/*
 * cfprefsd_testdumper.c - protocol discovery tool (development scaffolding).
 *
 * Registers a Mach service listener on a .test service name and dumps every
 * XPC message that system CoreFoundation sends to it.  Its only purpose is to
 * reveal the real wire contract (command names, keys, value types, reply
 * shapes) so CFPrefsXPCProtocol.h can be corrected to match reality instead of
 * being guessed at.
 *
 * It is a listener on .test names ONLY.  It never registers, connects to, or
 * otherwise touches com.apple.cfprefsd.agent or .daemon, so it cannot displace
 * the running system daemon.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <xpc/xpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *g_service = "com.apple.cfprefsd.agent.test";
static FILE *g_out = NULL;

static void dump(xpc_object_t obj, int depth, const char *label);

static void indent(int depth) {
    int i;
    for (i = 0; i < depth * 2; i++) fputc(' ', g_out);
}

static const char *type_name(xpc_object_t o) {
    xpc_type_t t = xpc_get_type(o);
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

static void dump(xpc_object_t obj, int depth, const char *label) {
    xpc_type_t t;
    if (!obj) {
        indent(depth); fprintf(g_out, "%s = <absent>\n", label ? label : "(value)");
        return;
    }
    t = xpc_get_type(obj);

    if (t == XPC_TYPE_DICTIONARY) {
        size_t n = xpc_dictionary_get_count(obj);
        indent(depth); fprintf(g_out, "%s%s: dict (%zu keys)\n", label ? label : "", label ? "" : "value", n);
        xpc_dictionary_apply(obj, ^bool(const char *key, xpc_object_t v){
            dump(v, depth + 1, key);
            return true;
        });
        return;
    }
    if (t == XPC_TYPE_ARRAY) {
        size_t n = xpc_array_get_count(obj), i;
        indent(depth); fprintf(g_out, "%s: array (%zu)\n", label ? label : "(array)", n);
        for (i = 0; i < n; i++) {
            char sub[32];
            snprintf(sub, sizeof(sub), "[%zu]", i);
            dump(xpc_array_get_value(obj, i), depth + 1, sub);
        }
        return;
    }
    if (t == XPC_TYPE_STRING) {
        indent(depth); fprintf(g_out, "%s = string \"%s\"\n", label ? label : "(value)",
                                xpc_string_get_string_ptr(obj));
        return;
    }
    if (t == XPC_TYPE_DATA) {
        size_t len = xpc_data_get_length(obj);
        indent(depth); fprintf(g_out, "%s = data (%zu bytes)\n", label ? label : "(value)", len);
        return;
    }
    if (t == XPC_TYPE_INT64) { indent(depth); fprintf(g_out, "%s = int64 %lld\n", label?label:"(value)", (long long)xpc_int64_get_value(obj)); return; }
    if (t == XPC_TYPE_UINT64){ indent(depth); fprintf(g_out, "%s = uint64 %llu\n", label?label:"(value)", (unsigned long long)xpc_uint64_get_value(obj)); return; }
    if (t == XPC_TYPE_DOUBLE){ indent(depth); fprintf(g_out, "%s = double %f\n", label?label:"(value)", xpc_double_get_value(obj)); return; }
    if (t == XPC_TYPE_BOOL)  { indent(depth); fprintf(g_out, "%s = bool %s\n", label?label:"(value)", xpc_bool_get_value(obj)?"true":"false"); return; }
    if (t == XPC_TYPE_DATE)  { indent(depth); fprintf(g_out, "%s = date %lld\n", label?label:"(value)", (long long)xpc_date_get_value(obj)); return; }
    if (t == XPC_TYPE_NULL)  { indent(depth); fprintf(g_out, "%s = null\n", label?label:"(value)"); return; }
    indent(depth); fprintf(g_out, "%s = <%s>\n", label ? label : "(value)", type_name(obj));
}

int main(int argc, char **argv) {
    xpc_connection_t listener;
    const char *outpath = argc > 1 ? argv[1] : "/tmp/cfprefsd-wire.txt";

    g_out = strcmp(outpath, "-") == 0 ? stderr : fopen(outpath, "w");
    if (!g_out) { perror("fopen"); return 1; }
    setvbuf(g_out, NULL, _IOLBF, 0);

    if (argc > 2) g_service = argv[2];

    fprintf(g_out, "### listening on %s\n", g_service);
    fflush(g_out);

    listener = xpc_connection_create_mach_service(g_service, NULL,
                                                  XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if (!listener) {
        fprintf(g_out, "### FAILED to create listener for %s\n", g_service);
        fflush(g_out);
        return 1;
    }
    fprintf(g_out, "### listener created, accepting\n");
    fflush(g_out);

    xpc_connection_set_event_handler(listener, ^(xpc_object_t event){
        xpc_type_t t = xpc_get_type(event);
        if (t == XPC_TYPE_ERROR) {
            fprintf(g_out, "### listener error, shutting down\n"); fflush(g_out);
            exit(0);
        }
        if (t == XPC_TYPE_CONNECTION) {
            xpc_connection_t peer = (xpc_connection_t)event;
            xpc_connection_set_event_handler(peer, ^(xpc_object_t msg){
                xpc_type_t mt = xpc_get_type(msg);
                if (mt == XPC_TYPE_ERROR) { xpc_connection_cancel(peer); return; }
                fprintf(g_out, "\n=== REQUEST ===\n");
                dump(msg, 0, NULL);
                fflush(g_out);

                /* Reply with an empty but well-formed dictionary so the client
                 * does not hang waiting for a response. */
                xpc_object_t reply = xpc_dictionary_create_reply(msg);
                if (reply) {
                    xpc_dictionary_set_bool(reply, "reply", true);
                    xpc_connection_send_message(peer, reply);
                    xpc_release(reply);
                }
            });
            xpc_connection_resume(peer);
        }
    });
    xpc_connection_resume(listener);
    dispatch_main();
    return 0;
}