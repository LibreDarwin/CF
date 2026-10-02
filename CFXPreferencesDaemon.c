/*
 * CFXPreferencesDaemon.c - the preferences XPC daemon.
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
 * This file implements __CFXPreferencesDaemon_main, the entry point the
 * cfprefsd shim tail-calls.  Storage is delegated to the ordinary
 * CFPreferences file layer, so the daemon owns the XPC conversation and the
 * notification bookkeeping while CFPreferences.c remains the only thing that
 * knows about plist files.
 *
 * The wire contract lives in CFPrefsXPCProtocol.h.
 *
 * XPC API notes for this SDK, all verified by probe rather than assumption:
 *   - Dictionaries are plain xpc_object_t; there is no xpc_dictionary_t.
 *   - xpc_dictionary_get_string() and friends REQUIRE a real dictionary and
 *     trap on any other type.  To read a bare value use xpc_string_get_string_ptr(),
 *     xpc_data_get_bytes_ptr(), xpc_bool_get_value(), xpc_double_get_value(),
 *     xpc_date_get_value(), xpc_array_get_value(), xpc_dictionary_get_count().
 *   - xpc_date_create() takes SECONDS, not milliseconds.
 *   - xpc_dictionary_apply()'s block returns bool: return true to continue.
 *   - xpc_dictionary_create_reply() returns NULL unless the message arrived on
 *     a connection.
 *   - A listener is created with XPC_CONNECTION_MACH_SERVICE_LISTENER, and the
 *     event delivered to its handler IS the accepted peer connection.
 */

#include <xpc/xpc.h>

#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/CFData.h>
#include <CoreFoundation/CFDate.h>
#include <CoreFoundation/CFNumber.h>
#include <CoreFoundation/CFPropertyList.h>
#include <CoreFoundation/CFString.h>
#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CFDictionary.h>
#include <CoreFoundation/CFPreferences.h>

#include "CFInternal.h"
#include "CFPrefsXPCProtocol.h"

#if DEPLOYMENT_TARGET_MACOSX

/* Defined by CFPreferences.c (CONST_STRING_DECL); declared here so the daemon
 * can default unresolved domains to Any*.  extern matters: a bare declaration
 * at file scope is a tentative definition in C and would duplicate the symbol. */
extern const CFStringRef kCFPreferencesAnyApplication;
extern const CFStringRef kCFPreferencesAnyHost;
extern const CFStringRef kCFPreferencesAnyUser;

/* One XPC peer we may need to notify.  XPC objects are not CF objects, so the
 * list is a plain C array holding retained connections rather than a CFArray
 * (which would CFRetain an XPC object and corrupt its refcount). */
typedef struct {
    xpc_connection_t  *conns;
    size_t             count;
    size_t             capacity;
    pthread_mutex_t    lock;
} CFPrefsPeerList;

typedef struct __CFPrefsDaemonInfo {
    CFPrefsRole         role;
    Boolean             testMode;
    xpc_connection_t    listener;
    CFPrefsPeerList     peers;
} __CFPrefsDaemonInfo;

static __CFPrefsDaemonInfo *__CFPrefsDaemon = NULL;

/* ---------------------------------------------------------------------- *
 * Peer bookkeeping
 * ---------------------------------------------------------------------- */

static void __CFPrefsPeersAdd(CFPrefsPeerList *peers, xpc_connection_t conn) {
    size_t i;
    pthread_mutex_lock(&peers->lock);
    for (i = 0; i < peers->count; i++) {
        if (peers->conns[i] == conn) {
            pthread_mutex_unlock(&peers->lock);
            return;                 /* already registered */
        }
    }
    if (peers->count == peers->capacity) {
        size_t newCap = peers->capacity ? peers->capacity * 2 : 8;
        xpc_connection_t *grown = realloc(peers->conns, newCap * sizeof(*grown));
        if (!grown) {
            pthread_mutex_unlock(&peers->lock);
            return;
        }
        peers->conns = grown;
        peers->capacity = newCap;
    }
    peers->conns[peers->count++] = conn;
    xpc_retain(conn);
    pthread_mutex_unlock(&peers->lock);
}

static void __CFPrefsPeersRemove(CFPrefsPeerList *peers, xpc_connection_t conn) {
    size_t i;
    pthread_mutex_lock(&peers->lock);
    for (i = 0; i < peers->count; i++) {
        if (peers->conns[i] == conn) {
            peers->conns[i] = peers->conns[--peers->count];
            xpc_release(conn);
            break;
        }
    }
    pthread_mutex_unlock(&peers->lock);
}

/* ---------------------------------------------------------------------- *
 * CFString <-> XPC
 * ---------------------------------------------------------------------- */

static CFStringRef __CFPrefsCopyStringFromXPC(xpc_object_t msg, const char *key) {
    const char *s = xpc_dictionary_get_string(msg, key);
    if (!s) return NULL;
    return CFStringCreateWithCString(kCFAllocatorDefault, s, kCFStringEncodingUTF8);
}

/* Returns a malloc'd NUL-terminated UTF-8 copy of a CFString, or NULL.  4 bytes
 * per UTF-16 unit is an upper bound for UTF-8, so this cannot truncate. */
static char *__CFPrefsCopyCString(CFStringRef str) {
    CFIndex len = CFStringGetLength(str);
    char *buf = malloc((size_t)len * 4 + 1);
    if (!buf) return NULL;
    if (!CFStringGetCString(str, buf, (size_t)len * 4 + 1, kCFStringEncodingUTF8)) {
        free(buf);
        return NULL;
    }
    return buf;
}

static xpc_object_t __CFPrefsCopyXPCString(CFStringRef str) {
    char *buf = __CFPrefsCopyCString(str);
    xpc_object_t result;
    if (!buf) return xpc_null_create();
    result = xpc_string_create(buf);
    free(buf);
    return result;
}

/* ---------------------------------------------------------------------- *
 * Property list <-> XPC
 * ---------------------------------------------------------------------- */

static CFTypeRef __CFPrefsCopyPropertyListFromXPC(xpc_object_t value);

static CFDictionaryRef __CFPrefsCopyDictionaryFromXPC(xpc_object_t value) {
    size_t count;
    __block size_t i = 0;
    const char **keys;
    CFMutableDictionaryRef result;
    if (!value || xpc_get_type(value) != XPC_TYPE_DICTIONARY) return NULL;
    count = xpc_dictionary_get_count(value);
    keys = calloc(count ? count : 1, sizeof(*keys));
    if (!keys) return NULL;
    xpc_dictionary_apply(value, ^bool(const char *key, xpc_object_t v){
        (void)v;
        if (i < count) keys[i++] = key;
        return true;                /* keep iterating */
    });
    result = CFDictionaryCreateMutable(kCFAllocatorDefault, (CFIndex)i,
                                      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!result) {
        free(keys);
        return NULL;
    }
    for (i = 0; i < count && keys[i]; i++) {
        xpc_object_t v = xpc_dictionary_get_value(value, keys[i]);
        CFStringRef k = CFStringCreateWithCString(kCFAllocatorDefault, keys[i], kCFStringEncodingUTF8);
        CFTypeRef pl = __CFPrefsCopyPropertyListFromXPC(v);
        if (k && pl && CFGetTypeID(pl) != CFNullGetTypeID()) {
            CFDictionarySetValue(result, k, pl);
        }
        if (k) CFRelease(k);
        if (pl) CFRelease(pl);
    }
    free(keys);
    return result;
}

static CFTypeRef __CFPrefsCopyPropertyListFromXPC(xpc_object_t value) {
    xpc_type_t t;
    if (!value) return NULL;
    t = xpc_get_type(value);

    if (t == XPC_TYPE_NULL) return CFRetain(kCFNull);
    if (t == XPC_TYPE_STRING) {
        return CFStringCreateWithCString(kCFAllocatorDefault, xpc_string_get_string_ptr(value), kCFStringEncodingUTF8);
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
        /* xpc_date_create() takes seconds. */
        return CFDateCreate(kCFAllocatorDefault, (CFAbsoluteTime)xpc_date_get_value(value));
    }
    if (t == XPC_TYPE_DATA) {
        size_t len = xpc_data_get_length(value);
        return CFDataCreate(kCFAllocatorDefault, (const UInt8 *)xpc_data_get_bytes_ptr(value), (CFIndex)len);
    }
    if (t == XPC_TYPE_ARRAY) {
        size_t count = xpc_array_get_count(value), idx;
        CFMutableArrayRef a = CFArrayCreateMutable(kCFAllocatorDefault, (CFIndex)count, &kCFTypeArrayCallBacks);
        if (!a) return NULL;
        for (idx = 0; idx < count; idx++) {
            CFTypeRef e = __CFPrefsCopyPropertyListFromXPC(xpc_array_get_value(value, idx));
            if (e) {
                CFArrayAppendValue(a, e);
                CFRelease(e);
            }
        }
        return a;
    }
    if (t == XPC_TYPE_DICTIONARY) {
        return __CFPrefsCopyDictionaryFromXPC(value);
    }
    return NULL;
}

/* ---------------------------------------------------------------------- *
 * Notification fan-out
 * ---------------------------------------------------------------------- */

static void __CFPrefsDaemonPostChangeNotification(CFStringRef domain) {
    __CFPrefsDaemonInfo *d = __CFPrefsDaemon;
    xpc_object_t *snapshot;
    size_t i, n;
    xpc_object_t msg;

    if (!d || !domain) return;

    pthread_mutex_lock(&d->peers.lock);
    n = d->peers.count;
    snapshot = malloc((n ? n : 1) * sizeof(*snapshot));
    if (snapshot) {
        for (i = 0; i < n; i++) {
            snapshot[i] = d->peers.conns[i];
            xpc_retain(snapshot[i]);
        }
    }
    pthread_mutex_unlock(&d->peers.lock);
    if (!snapshot) return;

    msg = xpc_dictionary_create_empty();
    if (msg) {
        /* NOT MEASURED: unlike every other key in this file, the daemon-initiated
         * change notification has not been observed on the wire.  The shape
         * below is our own convention, agreed with CFPrefsXPCClient.c; it is not
         * byte-compatible with the shipped daemon. */
        xpc_dictionary_set_int64(msg, CFPrefsNotifyOperation, CFPrefsOpValue);
        xpc_dictionary_set_bool(msg, CFPrefsNotifyIsNotification, true);
        {
            xpc_object_t dom = __CFPrefsCopyXPCString(domain);
            xpc_dictionary_set_value(msg, CFPrefsKeyDomain, dom);
            xpc_release(dom);
        }
        for (i = 0; i < n; i++) {
            xpc_connection_send_message(snapshot[i], msg);
        }
        xpc_release(msg);
    }

    for (i = 0; i < n; i++) xpc_release(snapshot[i]);
    free(snapshot);
}

/* ---------------------------------------------------------------------- *
 * Wire value codec
 *
 * MEASURED: the top-level "Value" carries a CFString as a native XPC string,
 * and every other property list type as a binary plist inside XPC data.  The
 * generic recursive codec above is still correct for values *nested* inside a
 * structured plist, so the wire codec is layered on top of it rather than
 * replacing it.
 * ---------------------------------------------------------------------- */

static CFTypeRef __CFPrefsCopyValueFromXPC(xpc_object_t value) {
    if (!value) return NULL;
    if (xpc_get_type(value) == XPC_TYPE_DATA) {
        size_t len = xpc_data_get_length(value);
        CFDataRef d = CFDataCreate(kCFAllocatorDefault,
                                   (const UInt8 *)xpc_data_get_bytes_ptr(value), (CFIndex)len);
        /* NOTE: this tree declares CFPropertyListCreateWithData() with the older
         * five-argument form (allocator, data, options, format, error). */
        CFPropertyListRef pl = CFPropertyListCreateWithData(kCFAllocatorDefault, d,
                                                           kCFPropertyListImmutable, NULL, NULL);
        if (d) CFRelease(d);
        return pl ? (CFTypeRef)pl : NULL;
    }
    return __CFPrefsCopyPropertyListFromXPC(value);
}

static xpc_object_t __CFPrefsCopyXPCValueFromPropertyList(CFTypeRef plist) {
    if (!plist) return xpc_null_create();
    if (CFGetTypeID(plist) == CFStringGetTypeID()) {
        return __CFPrefsCopyXPCString((CFStringRef)plist);
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
 * Request handling
 * ---------------------------------------------------------------------- */

/* The wire carries the user as the *symbolic constant name*, not a resolved
 * username: a capture of a current-user read showed the literal string
 * "kCFPreferencesCurrentUser".  Map those back onto the CFStringRef constants.
 * Anything else is treated as a literal user name. */
static CFStringRef __CFPrefsCopyUserFromXPC(xpc_object_t msg) {
    CFStringRef usr = __CFPrefsCopyStringFromXPC(msg, CFPrefsKeyUser);
    if (!usr) return (CFStringRef)CFRetain(kCFPreferencesAnyUser);
    if (CFEqual(usr, kCFPreferencesCurrentUser)) { CFRelease(usr); return (CFStringRef)CFRetain(kCFPreferencesCurrentUser); }
    if (CFEqual(usr, kCFPreferencesAnyUser))    { CFRelease(usr); return (CFStringRef)CFRetain(kCFPreferencesAnyUser); }
    return usr;   /* literal user name */
}

/* There is no host string on the wire.  A host-scoped request is flagged with
 * CFPreferencesIsByHost and resolved against the current host, which is the
 * only host the shipped client asks for by default. */
static CFStringRef __CFPrefsCopyHostFromXPC(xpc_object_t msg) {
    (void)msg;
    return (CFStringRef)CFRetain(kCFPreferencesCurrentHost);
}

static void __CFPrefsGetDomainTriple(xpc_object_t msg, CFStringRef *domain, CFStringRef *user, CFStringRef *host) {
    CFStringRef dom = __CFPrefsCopyStringFromXPC(msg, CFPrefsKeyDomain);
    *domain = dom ? dom : (CFStringRef)CFRetain(kCFPreferencesAnyApplication);
    *user   = __CFPrefsCopyUserFromXPC(msg);
    *host   = __CFPrefsCopyHostFromXPC(msg);
}

static void __CFPrefsSetError(xpc_object_t reply, int64_t type, const char *desc) {
    xpc_dictionary_set_int64(reply, CFPrefsReplyErrorType, (int64_t)type);
    xpc_dictionary_set_string(reply, CFPrefsReplyErrorDesc, desc);
    xpc_dictionary_set_bool(reply, CFPrefsReplyClientFault, false);
}

/* Send the property list half of a reply using the wire value encoding. */
static void __CFPrefsSetReplyValue(xpc_object_t reply, CFTypeRef plist) {
    if (!plist) return;
    if (CFGetTypeID(plist) == CFNullGetTypeID()) return;   /* absent, not null */
    {
        xpc_object_t x = __CFPrefsCopyXPCValueFromPropertyList(plist);
        if (x) {
            xpc_dictionary_set_value(reply, CFPrefsReplyPropertyList, x);
            xpc_release(x);
        }
    }
}

static void __CFPrefsDaemonHandleMessage(xpc_object_t msg, xpc_connection_t conn) {
    __CFPrefsDaemonInfo *d = __CFPrefsDaemon;
    xpc_object_t reply;
    int64_t op;
    CFStringRef domain = NULL, user = NULL, host = NULL;

    if (!d || !msg || xpc_get_type(msg) != XPC_TYPE_DICTIONARY) return;

    /* The command selector is an integer; there is no command string.  A message
     * without one is not a request. */
    if (!xpc_dictionary_get_int64(msg, CFPrefsKeyOperation)) return;
    op = xpc_dictionary_get_int64(msg, CFPrefsKeyOperation);

    reply = xpc_dictionary_create_reply(msg);
    if (!reply) return;

    __CFPrefsGetDomainTriple(msg, &domain, &user, &host);

    switch (op) {

    case CFPrefsOpValue: {
        /* Key is present for single-key work; its absence means the operation
         * applies to the whole domain.
         *
         * NOT MEASURED: how a removal is spelled.  Inferring it from "Value is
         * absent" is wrong -- absence is also how a read is spelled, so a
         * client reading a key would silently delete it.  Removal therefore
         * requires Value to be *present and null*.  This is our convention, not
         * a claim about the shipped daemon. */
        xpc_object_t rawKey = xpc_dictionary_get_value(msg, CFPrefsKeyKey);
        xpc_object_t rawVal = xpc_dictionary_get_value(msg, CFPrefsKeyValue);
        if (rawKey && xpc_get_type(rawKey) == XPC_TYPE_STRING) {
            CFStringRef key = __CFPrefsCopyStringFromXPC(msg, CFPrefsKeyKey);
            if (rawVal) {
                if (xpc_get_type(rawVal) == XPC_TYPE_NULL) {
                    CFPreferencesSetValue(key, NULL, domain, user, host);   /* removal */
                } else {
                    CFTypeRef pl = __CFPrefsCopyValueFromXPC(rawVal);
                    if (pl) {
                        CFPreferencesSetValue(key, pl, domain, user, host);
                        CFRelease(pl);
                    }
                }
                __CFPrefsDaemonPostChangeNotification(domain);
            } else if (key) {
                /* Read: answer with this key's value. */
                CFTypeRef pl = CFPreferencesCopyValue(key, domain, user, host);
                if (pl) {
                    __CFPrefsSetReplyValue(reply, pl);
                    CFRelease(pl);
                }
            }
            if (key) CFRelease(key);
        } else {
            /* No key: answer with the whole domain. */
            CFDictionaryRef r = CFPreferencesCopyMultiple(NULL, domain, user, host);
            if (r) {
                __CFPrefsSetReplyValue(reply, r);
                CFRelease(r);
            }
        }
        break;
    }

    case CFPrefsOpDomainWide: {
        /* Domain only, no key: the full dictionary for that domain. */
        CFDictionaryRef r = CFPreferencesCopyMultiple(NULL, domain, user, host);
        if (r) {
            __CFPrefsSetReplyValue(reply, r);
            CFRelease(r);
        }
        break;
    }

    case CFPrefsOpUserSync: {
        xpc_dictionary_set_bool(reply, CFPrefsReplyPropertyList,
            CFPreferencesSynchronize(domain, user, host) ? true : false);
        break;
    }

    case CFPrefsOpBatch: {
        /* "CFPreferencesMessages" carries an array of nested request dicts.  Each
         * one is answered in its own reply, in order. */
        xpc_object_t msgs = xpc_dictionary_get_value(msg, CFPrefsKeyMessages);
        size_t count = (msgs && xpc_get_type(msgs) == XPC_TYPE_ARRAY)
                     ? xpc_array_get_count(msgs) : 0;
        size_t i;
        for (i = 0; i < count; i++) {
            xpc_object_t sub = xpc_array_get_value(msgs, i);
            if (sub && xpc_get_type(sub) == XPC_TYPE_DICTIONARY) {
                CFStringRef sd, su, sh;
                int64_t sop;
                __CFPrefsGetDomainTriple(sub, &sd, &su, &sh);
                sop = xpc_dictionary_get_int64(sub, CFPrefsKeyOperation);
                if (sop == CFPrefsOpValue && xpc_dictionary_get_value(sub, CFPrefsKeyKey)) {
                    CFStringRef k = __CFPrefsCopyStringFromXPC(sub, CFPrefsKeyKey);
                    CFTypeRef pl = __CFPrefsCopyValueFromXPC(xpc_dictionary_get_value(sub, CFPrefsKeyValue));
                    if (k) {
                        CFPreferencesSetValue(k, pl && CFGetTypeID(pl) != CFNullGetTypeID() ? pl : NULL,
                                              sd, su, sh);
                        CFRelease(k);
                    }
                    if (pl) CFRelease(pl);
                    __CFPrefsDaemonPostChangeNotification(sd);
                }
                CFRelease(sd); CFRelease(su); CFRelease(sh);
            }
        }
        break;
    }

    case CFPrefsOpObserver:
        /* Register the peer for change broadcasts.  The shipped daemon tracks
         * observers per domain; this implementation broadcasts to every
         * registered peer, which is correct but less selective. */
        __CFPrefsPeersAdd(&d->peers, conn);
        break;

    case CFPrefsOpSpecial:
        /* Handled before every other key in the shipped daemon; semantics not
         * yet characterised, so accept and acknowledge. */
        break;

    case CFPrefsOpUnknown3:
    default:
        __CFPrefsSetError(reply, 1, "Unsupported CFPreferences Daemon Operation");
        break;
    }

    CFRelease(domain);
    CFRelease(user);
    CFRelease(host);
    xpc_connection_send_message(conn, reply);
    xpc_release(reply);
}

/* ---------------------------------------------------------------------- *
 * Connection lifecycle
 * ---------------------------------------------------------------------- */

static void __CFPrefsDaemonPeerEvent(xpc_connection_t peer, xpc_object_t event) {
    __CFPrefsDaemonInfo *d = __CFPrefsDaemon;
    xpc_type_t t = xpc_get_type(event);

    if (t == XPC_TYPE_ERROR) {
        /* peer died or was cancelled */
        if (d) __CFPrefsPeersRemove(&d->peers, peer);
        xpc_connection_cancel(peer);
        return;
    }
    __CFPrefsDaemonHandleMessage(event, peer);
}

static void __CFPrefsDaemonListenerEvent(xpc_connection_t listener, xpc_object_t event) {
    __CFPrefsDaemonInfo *d = __CFPrefsDaemon;
    xpc_type_t t = xpc_get_type(event);

    if (t == XPC_TYPE_ERROR) {
        if (d && d->listener == listener) {
            /* The service went away; nothing left to serve. */
            exit(1);
        }
        return;
    }
    if (t == XPC_TYPE_CONNECTION) {
        /* The accepted connection is delivered directly; no peer creation. */
        xpc_connection_t peer = (xpc_connection_t)event;
        xpc_connection_set_event_handler(peer, ^(xpc_object_t e){
            __CFPrefsDaemonPeerEvent(peer, e);
        });
        xpc_connection_resume(peer);
    }
}

/* Must be exported, not CF_PRIVATE: cfprefsd links against this symbol and
 * CF_PRIVATE expands to visibility("hidden"). */
CF_EXPORT int __CFXPreferencesDaemon_main(int argc, char **argv);

CF_EXPORT int __CFXPreferencesDaemon_main(int argc, char **argv) {
    __CFPrefsDaemonInfo *d;
    const char *serviceName;
    Boolean testMode;

    /* The role is chosen by getuid(), not by argv.  The shipped binary reads
     * argv[1] only to decide whether it was invoked directly at all. */
    (void)argc; (void)argv;
    if (argc < 2) {
        printf("cfprefsd is not intended to be used directly\n");
        return 0;
    }

    /* This process uses the public CFPreferences API as its storage layer, so it
     * must never route those calls back out over XPC. */
    __CFPrefsXPCClientBypass = true;

    testMode = getenv(CFPrefsTestDaemonEnvVar) != NULL;
    serviceName = (getuid() == 0)
        ? (testMode ? CFPrefsServiceDaemonTest : CFPrefsServiceDaemon)
        : (testMode ? CFPrefsServiceAgentTest  : CFPrefsServiceAgent);

    d = calloc(1, sizeof(*d));
    if (!d) return 1;
    d->role = (getuid() == 0) ? CFPrefsRoleDaemon : CFPrefsRoleAgent;
    d->testMode = testMode;
    pthread_mutex_init(&d->peers.lock, NULL);
    __CFPrefsDaemon = d;

    signal(SIGTERM, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    d->listener = xpc_connection_create_mach_service(serviceName, NULL,
                                                    XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if (!d->listener) {
        fprintf(stderr, "cfprefsd: could not listen on service %s\n", serviceName);
        return 1;
    }
    xpc_connection_set_event_handler(d->listener, ^(xpc_object_t e){
        __CFPrefsDaemonListenerEvent(d->listener, e);
    });
    xpc_connection_resume(d->listener);

    /* The entry point is reached by a tail call from the cfprefsd shim, so
     * returning here would tear the process down and kill the listener.  Serve
     * until the service dies (the listener handler exits in that case). */
    dispatch_main();

    /* not reached */
    return 0;
}

#endif /* DEPLOYMENT_TARGET_MACOSX */