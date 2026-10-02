/*
 * Copyright (C) 2026, LibreDarwin
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Clean-room reimplementation of /usr/sbin/cfprefsd.
 *
 * Apple's binary is a thin shim.  Before entering the preferences daemon it
 * relabels itself with libquarantine so the daemon's sandbox profile is
 * applied to the right identifier, then tail-calls the daemon entry point
 * that CoreFoundation exports:
 *
 *     int __CFXPreferencesDaemon_main(int argc, char **argv);
 *
 * The daemon distinguishes its two roles (per-system "daemon" and per-user
 * "agent") from the Mach service launchd bootstraps for it, not from argv.
 * Both roles share the same entry point and the same shim.
 */

#include <os/log.h>
#include <stdlib.h>
#include <unistd.h>

/* From libquarantine, which has no public header.  Apple passes 0x6 here. */
extern void *_qtn_proc_alloc(void);
extern void _qtn_proc_set_identifier(void *proc, const char *identifier);
extern void _qtn_proc_set_flags(void *proc, uint32_t flags);
extern int _qtn_proc_apply_to_self(void *proc);
extern void _qtn_proc_free(void *proc);

#define QTN_FLAG_USER_APPROVED 0x2
#define QTN_FLAG_QUARANTINE_APPROVED 0x4

extern int __CFXPreferencesDaemon_main(int argc, char **argv);

/* Apple keeps the two log messages in their own never-inlined functions. */
__attribute__((noinline)) static void log_alloc_failed(os_log_t log) {
    os_log_fault(log, "Failure to configure quarantine: qtn_proc_alloc() failed");
}

__attribute__((noinline)) static void log_apply_failed(os_log_t log) {
    os_log_fault(log, "Failure to configure quarantine: qtn_proc_apply_to_self() failed. Exiting daemon.");
}

__attribute__((noinline)) static void log_skipped_test_mode(void) {
    os_log_t log = os_log_create("com.apple.defaults", "cfprefsd");
    os_log_info(log, "Test mode: skipping quarantine census");
}

__attribute__((noinline)) static void census_quarantine_setup(void) {
    os_log_t log = os_log_create("com.apple.defaults", "cfprefsd");
    void *proc;
    int err;

    /* Test services (".test") are only ever bootstrapped from a throwaway plist
     * in a developer build.  Relabelling to com.apple.cfprefsd there requires
     * the quarantine approval this process deliberately does not have, and
     * _qtn_proc_apply_to_self() failing is fatal below -- which would make the
     * test service impossible to start at all.  The relabel only matters for
     * the production service's sandbox profile, so skip it when the test
     * override is in effect.  Production behaviour is unchanged. */
    if (getenv("__CFPreferencesTestDaemon") != NULL) {
        log_skipped_test_mode();
        return;
    }

    proc = _qtn_proc_alloc();

    if (proc == NULL) {
        log_alloc_failed(log);
        return;
    }

    _qtn_proc_set_identifier(proc, "com.apple.cfprefsd");
    _qtn_proc_set_flags(proc, QTN_FLAG_USER_APPROVED | QTN_FLAG_QUARANTINE_APPROVED);

    err = _qtn_proc_apply_to_self(proc);
    if (err != 0) {
        log_apply_failed(log);
        _qtn_proc_free(proc);
        exit(err);
    }

    _qtn_proc_free(proc);
}

int main(int argc, char **argv) {
    census_quarantine_setup();
    return __CFXPreferencesDaemon_main(argc, argv);
}
