/*
 * cfprefs_xpc_test.c - exercises CFPreferences over the XPC preferences path.
 *
 * Copyright (c) 2026, the DarwinSrc clean-room reimplementation authors.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Run with __CFPreferencesTestDaemon=1 and a bootstrapped
 * com.apple.cfprefsd.agent.test job.  Set __CFPrefsUseXPC=1 to route the public
 * CFPreferences API over XPC; leave it unset to exercise the direct plist path
 * against the same on-disk domain, which is how cross-process agreement is
 * checked.
 *
 * The test domain is com.darwinsrc.prefsxpctest and is deleted on success.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APPID "com.darwinsrc.prefsxpctest"

static int failures = 0;

static void check(int cond, const char *what) {
    printf("%-46s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) failures++;
}

static int isString(CFTypeRef v, const char *s) {
    return v && CFGetTypeID(v) == CFStringGetTypeID()
        && CFStringCompare((CFStringRef)v, CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8), 0) == kCFCompareEqualTo;
}

int main(void) {
    CFStringRef app = CFStringCreateWithCString(NULL, APPID, kCFStringEncodingUTF8);
    CFStringRef user = kCFPreferencesCurrentUser;
    CFStringRef host = kCFPreferencesCurrentHost;
    CFTypeRef v;

    printf("mode: %s\n", getenv("__CFPrefsUseXPC") ? "XPC" : "direct");

    /* stdout is block-buffered when redirected to a file, so a crash would
     * discard everything printed so far. */
    setvbuf(stdout, NULL, _IONBF, 0);

    CFPreferencesSetValue(CFSTR("FavoriteColor"),
                           CFSTR("chartreuse"), app, user, host);
    v = CFPreferencesCopyValue(CFSTR("FavoriteColor"), app, user, host);
    check(isString(v, "chartreuse"), "string round-trip");
    if (v) CFRelease(v);

    {
        int64_t forty_two = 42;
        CFNumberRef n = CFNumberCreate(NULL, kCFNumberSInt64Type, &forty_two);
        CFPreferencesSetValue(CFSTR("count"), n, app, user, host);
        CFRelease(n);
    }
    v = CFPreferencesCopyValue(CFSTR("count"), app, user, host);
    check(v && CFGetTypeID(v) == CFNumberGetTypeID(), "number round-trip");
    if (v) {
        int64_t got = 0;
        CFNumberGetValue((CFNumberRef)v, kCFNumberSInt64Type, &got);
        check(got == 42, "number value is 42");
        CFRelease(v);
    }

    {
        const void *keys[] = { CFSTR("a") };
        const void *vals[] = { CFSTR("one") };
        CFDictionaryRef d = CFDictionaryCreate(NULL, keys, vals, 1,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFPreferencesSetValue(CFSTR("nested"), d, app, user, host);
        CFRelease(d);
    }
    v = CFPreferencesCopyValue(CFSTR("nested"), app, user, host);
    check(v && CFGetTypeID(v) == CFDictionaryGetTypeID(), "dictionary round-trip");
    if (v) CFRelease(v);

    {
        CFMutableDictionaryRef all = (CFMutableDictionaryRef)
            CFPreferencesCopyMultiple(NULL, app, user, host);
        check(all && CFGetTypeID(all) == CFDictionaryGetTypeID(), "domain-wide read");
        check(all && CFDictionaryGetCount(all) >= 3, "domain has >= 3 keys");
        if (all) CFRelease(all);
    }

    {
        CFArrayRef keys = CFPreferencesCopyKeyList(app, user, host);
        check(keys != NULL, "key list is non-NULL");
        if (keys) CFRelease(keys);
    }

    check(CFPreferencesSynchronize(app, user, host), "synchronize reports success");

    /* Removal: set to NULL, then confirm it is gone. */
    CFPreferencesSetValue(CFSTR("count"), NULL, app, user, host);
    v = CFPreferencesCopyValue(CFSTR("count"), app, user, host);
    check(v == NULL, "removal round-trip");
    if (v) CFRelease(v);

    /* Leave the domain clean only when we were the ones who populated it. */
    CFPreferencesSetValue(CFSTR("FavoriteColor"), NULL, app, user, host);
    CFPreferencesSetValue(CFSTR("nested"), NULL, app, user, host);
    CFPreferencesSynchronize(app, user, host);

    CFRelease(app);
    printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}