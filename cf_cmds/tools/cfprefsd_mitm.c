/*
 * cfprefsd_mitm.c - protocol capture proxy (development scaffolding).
 *
 * Sits between system CoreFoundation and the real cfprefsd so both sides of
 * the wire contract can be observed for arbitrary operations.  It listens on a
 * .test service name; system CoreFoundation connects there when
 * __CFPreferencesTestDaemon is set, and the proxy forwards everything to the
 * real agent service.  Because requests and replies are both captured against
 * the genuine daemon, the opcode table, reply shape and error shape are
 * observed rather than inferred.
 *
 * The proxy is a pass-through: it never rewrites a payload, so the real daemon
 * sees exactly what it normally would.  It only ever listens on .test names and
 * connects to the system agent service as an ordinary client; it does not
 * register, displace or reconfigure com.apple.cfprefsd.agent.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <xpc/xpc.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *g_listen_name = "com.apple.cfprefsd.agent.test";
static const char *g_real_name   = "com.apple.cfprefsd.agent";
static FILE *g_out;
static unsigned long g_seq;

static void dump(xpc_object_t obj, int depth, const char *label);

static void indent(int depth) { int i; for (i = 0; i < depth * 2; i++) fputc(' ', g_out); }

static const char *type_name(xpc_type_t t) {
    if (t == XPC_TYPE_DICTIONARY) return "dict";
    if (t == XPC_TYPE_ARRAY)    return "array";
    if (t == XPC_TYPE_STRING)   return "string";
    if (t == XPC_TYPE_DATA)     return "data";
    if (t == XPC_TYPE_INT64)    return "int64";
    if (t == XPC_TYPE_UINT64)   return "uint64";
    if (t == XPC_TYPE_DOUBLE)   return "double";
    if (t == XPC_TYPE_BOOL)     return "bool";
    if (t == XPC_TYPE_DATE)     return "date";
    if (t == XPC_TYPE_NULL)     return "null";
    if (t == XPC_TYPE_ERROR)    return "error";
    if (t == XPC_TYPE_CONNECTION) return "connection";
    if (t == XPC_TYPE_ENDPOINT) return "endpoint";
    if (t == XPC_TYPE_UUID)     return "uuid";
    if (t == XPC_TYPE_FD)       return "fd";
    if (t == XPC_TYPE_ACTIVITY) return "activity";
    if (t == XPC_TYPE_SHMEM)    return "shmem";
    return "other";
}

static void dump_bytes(FILE *f, const void *b, size_t n) {
    size_t i;
    for (i = 0; i < n && i < 64; i++) fprintf(f, "%02x", ((const unsigned char *)b)[i]);
    if (n > 64) fputs("...", f);
}

/* Decode a plist blob so CFPropertyList-encoded values are readable. */
static void dump_plist_data(const void *b, size_t n) {
    CFDataRef d = CFDataCreate(NULL, b, (CFIndex)n);
    if (!d) return;
    CFPropertyListRef pl = NULL;
    if (CFPropertyListCreateWithData(NULL, d, kCFPropertyListImmutable, NULL, &pl) && pl) {
        CFStringRef s = CFCopyDescription(pl);
        if (s) {
            char buf[512];
            if (CFStringGetCString(s, buf, sizeof(buf), kCFStringEncodingUTF8)) {
                indent(2); fprintf(g_out, "    -> plist: %s\n", buf);
            }
            CFRelease(s);
        }
        CFRelease(pl);
    } else {
        indent(2); fprintf(g_out, "    -> (not a plist)\n");
    }
    CFRelease(d);
}

static void dump(xpc_object_t obj, int depth, const char *label) {
    xpc_type_t t;
    const char *pre = label ? label : "(value)";
    if (!obj) { indent(depth); fprintf(g_out, "%s = <absent>\n", pre); return; }
    t = xpc_get_type(obj);

    if (t == XPC_TYPE_DICTIONARY) {
        size_t n = xpc_dictionary_get_count(obj);
        indent(depth); fprintf(g_out, "%s: dict (%zu keys)\n", pre, n);
        xpc_dictionary_apply(obj, ^bool(const char *k, xpc_object_t v){
            dump(v, depth + 1, k); return true; });
        return;
    }
    if (t == XPC_TYPE_ARRAY) {
        size_t n = xpc_array_get_count(obj), i;
        indent(depth); fprintf(g_out, "%s: array (%zu)\n", pre, n);
        for (i = 0; i < n; i++) { char s[32]; snprintf(s, sizeof(s), "[%zu]", i);
            dump(xpc_array_get_value(obj, i), depth + 1, s); }
        return;
    }
    if (t == XPC_TYPE_STRING) { indent(depth); fprintf(g_out, "%s = string \"%s\"\n", pre, xpc_string_get_string_ptr(obj)); return; }
    if (t == XPC_TYPE_DATA) {
        size_t len = xpc_data_get_length(obj);
        indent(depth); fprintf(g_out, "%s = data (%zu bytes) = ", pre, len);
        dump_bytes(g_out, xpc_data_get_bytes_ptr(obj), len);
        fputc('\n', g_out);
        dump_plist_data(xpc_data_get_bytes_ptr(obj), len);
        return;
    }
    if (t == XPC_TYPE_INT64)  { indent(depth); fprintf(g_out, "%s = int64 %lld\n", pre, (long long)xpc_int64_get_value(obj)); return; }
    if (t == XPC_TYPE_UINT64) { indent(depth); fprintf(g_out, "%s = uint64 %llu\n", pre, (unsigned long long)xpc_uint64_get_value(obj)); return; }
    if (t == XPC_TYPE_DOUBLE) { indent(depth); fprintf(g_out, "%s = double %f\n", pre, xpc_double_get_value(obj)); return; }
    if (t == XPC_TYPE_BOOL)   { indent(depth); fprintf(g_out, "%s = bool %s\n", pre, xpc_bool_get_value(obj) ? "true" : "false"); return; }
    if (t == XPC_TYPE_DATE)   { indent(depth); fprintf(g_out, "%s = date %lld\n", pre, (long long)xpc_date_get_value(obj)); return; }
    indent(depth); fprintf(g_out, "%s = <%s>\n", pre, type_name(t));
}

typedef struct { xpc_connection_t client; xpc_connection_t real; } peer_ctx;

int main(int argc, char **argv) {
    xpc_connection_t listener;
    const char *outpath = argc > 1 ? argv[1] : "/tmp/cfprefsd-wire.txt";

    g_out = strcmp(outpath, "-") == 0 ? stderr : fopen(outpath, "w");
    if (!g_out) { perror("fopen"); return 1; }
    setvbuf(g_out, NULL, _IOLBF, 0);
    if (argc > 2) g_listen_name = argv[2];
    if (argc > 3) g_real_name   = argv[3];

    fprintf(g_out, "### proxy %s -> %s\n", g_listen_name, g_real_name); fflush(g_out);

    listener = xpc_connection_create_mach_service(g_listen_name, NULL, XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if (!listener) { fprintf(g_out, "### FAILED to listen on %s\n", g_listen_name); fflush(g_out); return 1; }

    xpc_connection_set_event_handler(listener, ^(xpc_object_t event){
        if (xpc_get_type(event) == XPC_TYPE_ERROR) { fprintf(g_out, "### listener error\n"); fflush(g_out); exit(0); }
        if (xpc_get_type(event) != XPC_TYPE_CONNECTION) return;

        xpc_connection_t client = (xpc_connection_t)event;
        peer_ctx *c = calloc(1, sizeof(*c));
        c->client = client;
        c->real = xpc_connection_create_mach_service(g_real_name, NULL, 0);
        if (!c->real) { fprintf(g_out, "### could not connect to real %s\n", g_real_name); fflush(g_out); free(c); return; }

        xpc_connection_set_event_handler(c->real, ^(xpc_object_t msg){
            if (xpc_get_type(msg) == XPC_TYPE_ERROR) return;      /* replies arrive via reply handler */
            fprintf(g_out, "\n--- ASYNC from daemon -> client ---\n");
            dump(msg, 0, NULL); fflush(g_out);
            xpc_connection_send_message(client, msg);
        });
        xpc_connection_resume(c->real);

        xpc_connection_set_event_handler(client, ^(xpc_object_t msg){
            if (xpc_get_type(msg) == XPC_TYPE_ERROR) {
                fprintf(g_out, "\n--- client connection closed ---\n"); fflush(g_out);
                xpc_connection_cancel(c->real); return;
            }
            unsigned long seq = ++g_seq;
            fprintf(g_out, "\n=== REQUEST #%lu ===\n", seq);
            dump(msg, 0, NULL); fflush(g_out);

            xpc_connection_send_message_with_reply(c->real, msg, NULL, ^(xpc_object_t reply){
                fprintf(g_out, "--- REPLY #%lu ---\n", seq);
                if (reply) { dump(reply, 0, NULL); xpc_connection_send_message(client, reply); }
                else fprintf(g_out, "  (no reply)\n");
                fflush(g_out);
            });
        });
        xpc_connection_resume(client);
        fprintf(g_out, "\n### client connected\n"); fflush(g_out);
    });
    xpc_connection_resume(listener);
    dispatch_main();
    return 0;
}