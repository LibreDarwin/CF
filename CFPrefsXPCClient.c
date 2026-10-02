/*
 * CFPrefsXPCClient.c - client side of the preferences XPC protocol.
 *
 * Copyright (c) 2026, the DarwinSrc clean-room reimplementation authors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * ------------------------------------------------------------------------
 *
 * WHY THIS FILE IS GATED
 *
 * Every public CFPreferences entry point still works by talking to the plist
 * on disk directly (see CFPreferences.c).  That path is correct, fast, and
 * already covered by the runtime and parity suites.  Routing it over XPC is
 * *not* a strict improvement: the daemon itself uses these same public
 * functions as its storage layer, so a client that always routes would make
 * the daemon call itself the moment it tried to save a value.
 *
 * So routing is opt-in through __CFPrefsUseXPC=1 and is additionally
 * suppressed inside the daemon process.  See __CFPrefsXPCClientShouldRoute.
 */

#if DEPLOYMENT_TARGET_MACOSX

#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFString.h>
#include <CoreFoundation/CFData.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFDate.h>
#include <CoreFoundation/CFPropertyList.h>
#include <CoreFoundation/CFPreferences.h>
#include <xpc/xpc.h>
#include <dispatch/dispatch.h>
#include <unistd.h>
#include <stdlib.h>

#include "CFInternal.h"
#include "CFPrefsXPCProtocol.h"

/* Set by __CFXPreferencesDaemon_main.  Without this the daemon's own use of
 * CFPreferences as a storage backend would recurse straight back into XPC. */
CF_PRIVATE Boolean __CFPrefsXPCClientBypass = false;

/* ---------------------------------------------------------------------- *
 * Gating and service selection
 * ---------------------------------------------------------------------- */

/* MEASURED: a restricted process never uses the ".test" services, even when the
 * test environment variable is set.  See CFPrefsXPCProtocol.h. */
static const char *__CFPrefsClientServiceName(void) {
    Boolean testMode = (getenv(CFPrefsTestDaemonEnvVar) != NULL)
                    && !__CFProcessIsRestricted();
    if (getuid() == 0) {
        return testMode ? CFPrefsServiceDaemonTest : CFPrefsServiceDaemon;
    }
    return testMode ? CFPrefsServiceAgentTest : CFPrefsServiceAgent;
}

CF_PRIVATE Boolean __CFPrefsXPCClientShouldRoute(void) {
    static int cached = -1;
    const char *flag;

    if (cached >= 0) return cached == 1;

    if (__CFPrefsXPCClientBypass) { cached = 0; return false; }
    if (getenv(CFPrefsAvoidDaemonEnvVar)) { cached = 0; return false; }

    flag = getenv("__CFPrefsUseXPC");
    if (!flag || strcmp(flag, "1") != 0) { cached = 0; return false; }

    cached = 1;
    return true;
}

/* ---------------------------------------------------------------------- *
 * Connection cache
 *
 * Only two services can ever be involved (agent and daemon), so a pair of
 * slots keyed by uid is enough.  Connections are never torn down: the daemon
 * lifetime is the process lifetime.
 * ---------------------------------------------------------------------- */

typedef struct {
    const char *name;
    xpc_connection_t conn;
} __CFPrefsClientSlot;

static __CFPrefsClientSlot __CFPrefsClientSlots[2] = {
    { CFPrefsServiceAgentTest,  NULL },
    { CFPrefsServiceDaemonTest, NULL },
};

static xpc_connection_t __CFPrefsClientGetConnection(void) {
    const char *name = __CFPrefsClientServiceName();
    __CFPrefsClientSlot *slot = &__CFPrefsClientSlots[getuid() == 0 ? 1 : 0];

    if (slot->conn && slot->name == name) {
        return slot->conn;
    }
    if (slot->conn) {
        xpc_connection_cancel(slot->conn);
        xpc_release(slot->conn);
        slot->conn = NULL;
    }
    slot->name = name;
    slot->conn = xpc_connection_create_mach_service(name, NULL, 0);
    if (slot->conn) {
        xpc_connection_resume(slot->conn);
    }
    return slot->conn;
}

/* ---------------------------------------------------------------------- *
 * String helpers
 * ---------------------------------------------------------------------- */

static CFStringRef __CFPrefsClientCopyCString(const char *s) {
    if (!s) return NULL;
    return CFStringCreateWithCString(kCFAllocatorDefault, s, kCFStringEncodingUTF8);
}

static char *__CFPrefsClientCopyCStringFromCFString(CFStringRef str) {
    CFIndex len;
    char *buf;
    if (!str) return NULL;
    len = CFStringGetLength(str);
    buf = malloc((size_t)len * 4 + 1);
    if (!buf) return NULL;
    if (!CFStringGetCString(str, buf, (size_t)len * 4 + 1, kCFStringEncodingUTF8)) {
        free(buf);
        return NULL;
    }
    return buf;
}

static xpc_object_t __CFPrefsClientCopyXPCString(CFStringRef str) {
    char *buf = __CFPrefsClientCopyCStringFromCFString(str);
    xpc_object_t result;
    if (!buf) return xpc_null_create();
    result = xpc_string_create(buf);
    free(buf);
    return result;
}

/* The wire carries the *symbolic constant name* for the user, not a resolved
 * user name: a capture of a current-user request showed the literal string
 * "kCFPreferencesCurrentUser". */
static xpc_object_t __CFPrefsClientCopyXPCUser(CFStringRef user) {
    if (!user || CFEqual(user, kCFPreferencesCurrentUser)) {
        return xpc_string_create("kCFPreferencesCurrentUser");
    }
    if (CFEqual(user, kCFPreferencesAnyUser)) {
        return xpc_string_create("kCFPreferencesAnyUser");
    }
    return __CFPrefsClientCopyXPCString(user);
}

/* ---------------------------------------------------------------------- *
 * Wire value codec
 *
 * MEASURED: CFString travels as a native XPC string; every other property list
 * type travels as a binary plist inside XPC data.
 * ---------------------------------------------------------------------- */

static CFTypeRef __CFPrefsClientCopyValueFromXPC(xpc_object_t value);

static CFDictionaryRef __CFPrefsClientCopyDictionaryFromXPC(xpc_object_t value) {
    size_t count, i;
    __block size_t idx = 0;
    const char **keys;
    CFMutableDictionaryRef result;

    if (!value || xpc_get_type(value) != XPC_TYPE_DICTIONARY) return NULL;
    count = xpc_dictionary_get_count(value);
    keys = calloc(count ? count : 1, sizeof(*keys));
    if (!keys) return NULL;
    xpc_dictionary_apply(value, ^bool(const char *key, xpc_object_t v){
        (void)v;
        if (idx < count) keys[idx++] = key;
        return true;
    });
    result = CFDictionaryCreateMutable(kCFAllocatorDefault, (CFIndex)idx,
                                      &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks);
    if (!result) { free(keys); return NULL; }
    for (i = 0; i < count && keys[i]; i++) {
        CFStringRef k = __CFPrefsClientCopyCString(keys[i]);
        CFTypeRef pl = __CFPrefsClientCopyValueFromXPC(xpc_dictionary_get_value(value, keys[i]));
        if (k && pl && CFGetTypeID(pl) != CFNullGetTypeID()) {
            CFDictionarySetValue(result, k, pl);
        }
        if (k) CFRelease(k);
        if (pl) CFRelease(pl);
    }
    free(keys);
    return result;
}

static CFTypeRef __CFPrefsClientCopyValueFromXPC(xpc_object_t value) {
    xpc_type_t t;
    if (!value) return NULL;
    t = xpc_get_type(value);

    if (t == XPC_TYPE_NULL) return CFRetain(kCFNull);
    if (t == XPC_TYPE_STRING) {
        return __CFPrefsClientCopyCString(xpc_string_get_string_ptr(value));
    }
    if (t == XPC_TYPE_BOOL) {
        return xpc_bool_get_value(value) ? kCFBooleanTrue : kCFBooleanFalse;
    }
    if (t == XPC_TYPE_INT64) {
        int64_t n = xpc_int64_get_value(value);
        return CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &n);
    }
    if (t == XPC_TYPE_DOUBLE) {
        double d = xpc_double_get_value(value);
        return CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &d);
    }
    if (t == XPC_TYPE_DATE) {
        return CFDateCreate(kCFAllocatorDefault, (CFAbsoluteTime)xpc_date_get_value(value));
    }
    if (t == XPC_TYPE_DATA) {
        /* The measured wire form for every non-string value: a binary plist. */
        size_t len = xpc_data_get_length(value);
        CFDataRef d = CFDataCreate(kCFAllocatorDefault,
                                   (const UInt8 *)xpc_data_get_bytes_ptr(value), (CFIndex)len);
        CFPropertyListRef pl;
        if (!d) return NULL;
        pl = CFPropertyListCreateWithData(kCFAllocatorDefault, d,
                                          kCFPropertyListImmutable, NULL, NULL);
        CFRelease(d);
        return pl ? (CFTypeRef)pl : NULL;
    }
    if (t == XPC_TYPE_ARRAY) {
        size_t count = xpc_array_get_count(value), i;
        CFMutableArrayRef a = CFArrayCreateMutable(kCFAllocatorDefault, (CFIndex)count,
                                                  &kCFTypeArrayCallBacks);
        if (!a) return NULL;
        for (i = 0; i < count; i++) {
            CFTypeRef e = __CFPrefsClientCopyValueFromXPC(xpc_array_get_value(value, i));
            if (e) { CFArrayAppendValue(a, e); CFRelease(e); }
        }
        return a;
    }
    if (t == XPC_TYPE_DICTIONARY) {
        return __CFPrefsClientCopyDictionaryFromXPC(value);
    }
    return NULL;
}

static xpc_object_t __CFPrefsClientCopyXPCValueFromPropertyList(CFTypeRef plist) {
    if (!plist) return xpc_null_create();
    if (CFGetTypeID(plist) == CFStringGetTypeID()) {
        return __CFPrefsClientCopyXPCString((CFStringRef)plist);
    }
    {
        CFDataRef d = CFPropertyListCreateData(kCFAllocatorDefault, plist,
                                               kCFPropertyListBinaryFormat_v1_0, 0, NULL);
        xpc_object_t result;
        if (!d) return xpc_null_create();
        result = xpc_data_create(CFDataGetBytePtr(d), (size_t)CFDataGetLength(d));
        CFRelease(d);
        return result;
    }
}

/* ---------------------------------------------------------------------- *
 * Request plumbing
 * ---------------------------------------------------------------------- */

/* A host-scoped request is flagged rather than named; the daemon resolves it
 * against the current host.  Anything else is left to the direct path because
 * the wire cannot express it. */
static Boolean __CFPrefsClientHostIsRoutable(CFStringRef host) {
    if (!host) return false;
    return CFEqual(host, kCFPreferencesCurrentHost) || CFEqual(host, kCFPreferencesAnyHost);
}

static void __CFPrefsClientFillContext(xpc_object_t msg, CFStringRef appName,
                                       CFStringRef user, CFStringRef host) {
    xpc_object_t dom, usr;

    dom = __CFPrefsClientCopyXPCString(appName);
    xpc_dictionary_set_value(msg, CFPrefsKeyDomain, dom);
    xpc_release(dom);

    usr = __CFPrefsClientCopyXPCUser(user);
    xpc_dictionary_set_value(msg, CFPrefsKeyUser, usr);
    xpc_release(usr);

    xpc_dictionary_set_bool(msg, CFPrefsKeyIsByHost,
                            CFEqual(host, kCFPreferencesCurrentHost));

    /* MEASURED: this is the client's process name, e.g. "ops" or "oneop". */
    {
        const char *prog = getprogname();
        if (prog) xpc_dictionary_set_string(msg, CFPrefsKeyHostBundleID, prog);
    }
}

/* Send and wait for the reply.  Returns NULL on timeout or teardown, which is
 * the signal for the caller to fall back to the direct path. */
static xpc_object_t __CFPrefsClientSend(xpc_object_t msg) {
    xpc_connection_t conn = __CFPrefsClientGetConnection();
    __block xpc_object_t reply = NULL;
    dispatch_semaphore_t done;

    if (!conn) return NULL;
    if (xpc_get_type(conn) == XPC_TYPE_ERROR) {
        /* Force a reconnect on the next attempt. */
        __CFPrefsClientSlots[getuid() == 0 ? 1 : 0].conn = NULL;
        return NULL;
    }

    done = dispatch_semaphore_create(0);
    xpc_connection_send_message_with_reply(conn, msg, NULL, ^(xpc_object_t r){
        if (xpc_get_type(r) == XPC_TYPE_DICTIONARY) reply = r;
        dispatch_semaphore_signal(done);
    });
    if (dispatch_semaphore_wait(done,
            dispatch_time(DISPATCH_TIME_NOW, 2ull * NSEC_PER_SEC)) != 0) {
        /* Timed out.  Drop the connection so the next call starts clean. */
        xpc_connection_cancel(conn);
        __CFPrefsClientSlots[getuid() == 0 ? 1 : 0].conn = NULL;
    }
    dispatch_release(done);
    return reply;
}

/* A reply describes a failure via a non-zero CFPreferencesErrorType. */
static Boolean __CFPrefsClientReplyOK(xpc_object_t reply) {
    if (!reply) return false;
    return xpc_dictionary_get_int64(reply, CFPrefsReplyErrorType) == 0;
}

static CFTypeRef __CFPrefsClientReplyValue(xpc_object_t reply) {
    xpc_object_t v;
    if (!__CFPrefsClientReplyOK(reply)) return NULL;
    v = xpc_dictionary_get_value(reply, CFPrefsReplyPropertyList);
    if (!v) return NULL;
    return __CFPrefsClientCopyValueFromXPC(v);
}

/* ---------------------------------------------------------------------- *
 * Public entry points, called from CFPreferences.c
 * ---------------------------------------------------------------------- */

/* Returns 1 and fills *out when the request was serviced over XPC, 0 when the
 * caller must fall back to the direct path. */
CF_PRIVATE int __CFPrefsXPCClientCopyValue(CFStringRef key, CFStringRef appName,
                                           CFStringRef user, CFStringRef host,
                                           CFTypeRef *out) {
    xpc_object_t msg;
    CFTypeRef v;
    char *kbuf;

    if (!key || !appName || !user || !host) return 0;
    if (!__CFPrefsClientHostIsRoutable(host)) return 0;

    msg = xpc_dictionary_create_empty();
    if (!msg) return 0;
    xpc_dictionary_set_int64(msg, CFPrefsKeyOperation, CFPrefsOpValue);
    kbuf = __CFPrefsClientCopyCStringFromCFString(key);
    if (kbuf) xpc_dictionary_set_string(msg, CFPrefsKeyKey, kbuf);
    free(kbuf);
    __CFPrefsClientFillContext(msg, appName, user, host);

    {
        xpc_object_t reply = __CFPrefsClientSend(msg);
        xpc_release(msg);
        v = __CFPrefsClientReplyValue(reply);
        if (reply) xpc_release(reply);
    }
    if (!v || CFGetTypeID(v) == CFNullGetTypeID()) {
        if (v) CFRelease(v);
        return 0;               /* not found: let the direct path decide */
    }
    *out = v;
    return 1;
}

CF_PRIVATE int __CFPrefsXPCClientCopyMultiple(CFArrayRef keysToFetch,
                                              CFStringRef appName,
                                              CFStringRef user, CFStringRef host,
                                              CFDictionaryRef *out) {
    xpc_object_t msg, reply;
    CFTypeRef d;

    if (!appName || !user || !host) return 0;
    if (!__CFPrefsClientHostIsRoutable(host)) return 0;

    msg = xpc_dictionary_create_empty();
    if (!msg) return 0;
    /* Domain-wide read: no Key, so the daemon answers with the whole domain. */
    xpc_dictionary_set_int64(msg, CFPrefsKeyOperation, CFPrefsOpDomainWide);
    __CFPrefsClientFillContext(msg, appName, user, host);

    reply = __CFPrefsClientSend(msg);
    xpc_release(msg);
    if (!__CFPrefsClientReplyOK(reply)) {
        if (reply) xpc_release(reply);
        return 0;
    }
    d = __CFPrefsClientReplyValue(reply);
    if (reply) xpc_release(reply);

    if (keysToFetch) {
        /* The daemon returned everything; narrow it to the requested keys. */
        CFMutableDictionaryRef narrowed;
        CFIndex idx, count;
        if (!d) return 0;
        if (CFGetTypeID(d) != CFDictionaryGetTypeID()) { CFRelease(d); return 0; }
        narrowed = CFDictionaryCreateMutable(kCFAllocatorDefault,
                                             CFArrayGetCount(keysToFetch),
                                             &kCFTypeDictionaryKeyCallBacks,
                                             &kCFTypeDictionaryValueCallBacks);
        if (!narrowed) { CFRelease(d); return 0; }
        count = CFArrayGetCount(keysToFetch);
        for (idx = 0; idx < count; idx++) {
            CFStringRef k = (CFStringRef)CFArrayGetValueAtIndex(keysToFetch, idx);
            CFTypeRef v = CFDictionaryGetValue((CFDictionaryRef)d, k);
            if (v) CFDictionarySetValue(narrowed, k, v);
        }
        CFRelease(d);
        *out = narrowed;
        return 1;
    }

    if (!d) return 0;
    if (CFGetTypeID(d) != CFDictionaryGetTypeID()) { CFRelease(d); return 0; }
    *out = (CFDictionaryRef)d;
    return 1;
}

/* value == NULL means removal.  Returns 1 when the daemon accepted the write. */
CF_PRIVATE int __CFPrefsXPCClientSetValue(CFStringRef key, CFTypeRef value,
                                          CFStringRef appName,
                                          CFStringRef user, CFStringRef host) {
    xpc_object_t msg, reply;
    char *kbuf;

    if (!key || !appName || !user || !host) return 0;
    if (!__CFPrefsClientHostIsRoutable(host)) return 0;

    msg = xpc_dictionary_create_empty();
    if (!msg) return 0;
    xpc_dictionary_set_int64(msg, CFPrefsKeyOperation, CFPrefsOpValue);
    kbuf = __CFPrefsClientCopyCStringFromCFString(key);
    if (kbuf) xpc_dictionary_set_string(msg, CFPrefsKeyKey, kbuf);
    free(kbuf);
    /* Write, read, and removal are three distinct shapes on opcode 1:
     *   Value present and non-null -> write
     *   Value present and null      -> removal
     *   Value absent                -> read
     * Omitting Value to mean "remove" would collide with the read, so a read
     * would silently delete the key. */
    if (value) {
        if (CFGetTypeID(value) == CFNullGetTypeID()) {
            xpc_dictionary_set_value(msg, CFPrefsKeyValue, xpc_null_create());
        } else {
            xpc_object_t v = __CFPrefsClientCopyXPCValueFromPropertyList(value);
            xpc_dictionary_set_value(msg, CFPrefsKeyValue, v);
            xpc_release(v);
        }
    }
    __CFPrefsClientFillContext(msg, appName, user, host);

    reply = __CFPrefsClientSend(msg);
    xpc_release(msg);
    if (!reply) return 0;
    xpc_release(reply);
    return 1;
}

/* Batched through CFPreferencesMessages, which the daemon reads as an array of
 * nested request dictionaries.  Returns 1 when the daemon accepted every entry.
 *
 * NOTE: the daemon's batch path answers once for the whole array, so the client
 * cannot tell which individual entry failed.  A malformed entry is therefore
 * invisible to the caller; if the batch comes back as an error the caller falls
 * back to the direct path, which re-applies the whole thing visibly. */
CF_PRIVATE int __CFPrefsXPCClientSetMultiple(CFDictionaryRef keysToSet,
                                             CFArrayRef keysToRemove,
                                             CFStringRef appName,
                                             CFStringRef user, CFStringRef host) {
    xpc_object_t msg, reply, subs = NULL;
    CFIndex idx, count;
    int sent = 0;

    if (!appName || !user || !host) return 0;
    if (!__CFPrefsClientHostIsRoutable(host)) return 0;
    if (!keysToSet && !keysToRemove) return 0;

    msg = xpc_dictionary_create_empty();
    if (!msg) return 0;
    xpc_dictionary_set_int64(msg, CFPrefsKeyOperation, CFPrefsOpBatch);
    __CFPrefsClientFillContext(msg, appName, user, host);

    if (keysToSet && (count = CFDictionaryGetCount(keysToSet)) > 0) {
        CFIndex bytes = (CFIndex)sizeof(const void *) * count * 2;
        const void **kv = (const void **)CFAllocatorAllocate(kCFAllocatorDefault, bytes, 0);
        if (!kv) { xpc_release(msg); return 0; }
        CFDictionaryGetKeysAndValues(keysToSet, &kv[0], &kv[count]);
        for (idx = 0; idx < count; idx++) {
            CFStringRef k = (CFStringRef)kv[idx];
            CFTypeRef v = (CFTypeRef)kv[count + idx];
            char *kbuf;
            xpc_object_t sub;
            if (!k || CFGetTypeID(k) != CFStringGetTypeID()) continue;
            if (!v || CFGetTypeID(v) == CFNullGetTypeID()) continue;
            sub = xpc_dictionary_create_empty();
            if (!sub) continue;
            xpc_dictionary_set_int64(sub, CFPrefsKeyOperation, CFPrefsOpValue);
            kbuf = __CFPrefsClientCopyCStringFromCFString(k);
            if (kbuf) xpc_dictionary_set_string(sub, CFPrefsKeyKey, kbuf);
            free(kbuf);
            {
                xpc_object_t xv = __CFPrefsClientCopyXPCValueFromPropertyList(v);
                xpc_dictionary_set_value(sub, CFPrefsKeyValue, xv);
                xpc_release(xv);
            }
            if (!subs) subs = xpc_array_create_empty();
            if (subs) { xpc_array_append_value(subs, sub); sent++; }
            xpc_release(sub);
        }
        CFAllocatorDeallocate(kCFAllocatorDefault, kv);
    }

    /* Removals are the same shape with Value omitted. */
    if (keysToRemove && (count = CFArrayGetCount(keysToRemove)) > 0) {
        for (idx = 0; idx < count; idx++) {
            CFStringRef k = (CFStringRef)CFArrayGetValueAtIndex(keysToRemove, idx);
            char *kbuf;
            xpc_object_t sub;
            if (!k || CFGetTypeID(k) != CFStringGetTypeID()) continue;
            sub = xpc_dictionary_create_empty();
            if (!sub) continue;
            xpc_dictionary_set_int64(sub, CFPrefsKeyOperation, CFPrefsOpValue);
            kbuf = __CFPrefsClientCopyCStringFromCFString(k);
            if (kbuf) xpc_dictionary_set_string(sub, CFPrefsKeyKey, kbuf);
            free(kbuf);
            if (!subs) subs = xpc_array_create_empty();
            if (subs) { xpc_array_append_value(subs, sub); sent++; }
            xpc_release(sub);
        }
    }

    if (!subs || sent == 0) {
        if (subs) xpc_release(subs);
        xpc_release(msg);
        return 0;
    }
    xpc_dictionary_set_value(msg, CFPrefsKeyMessages, subs);
    xpc_release(subs);

    reply = __CFPrefsClientSend(msg);
    xpc_release(msg);
    if (!reply) return 0;
    xpc_release(reply);
    return 1;
}

CF_PRIVATE int __CFPrefsXPCClientSynchronize(CFStringRef appName,
                                              CFStringRef user, CFStringRef host,
                                              Boolean *out) {
    xpc_object_t msg, reply;

    if (!appName || !user || !host) return 0;
    if (!__CFPrefsClientHostIsRoutable(host)) return 0;

    msg = xpc_dictionary_create_empty();
    if (!msg) return 0;
    xpc_dictionary_set_int64(msg, CFPrefsKeyOperation, CFPrefsOpUserSync);
    __CFPrefsClientFillContext(msg, appName, user, host);

    reply = __CFPrefsClientSend(msg);
    xpc_release(msg);
    if (!__CFPrefsClientReplyOK(reply)) {
        if (reply) xpc_release(reply);
        return 0;
    }
    if (out) *out = xpc_dictionary_get_bool(reply, CFPrefsReplyPropertyList);
    xpc_release(reply);
    return 1;
}

#endif /* DEPLOYMENT_TARGET_MACOSX */