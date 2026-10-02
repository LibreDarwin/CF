/*
 * CFPrefsXPCProtocol.h - wire contract between the preferences XPC client
 * and the preferences daemon (__CFXPreferencesDaemon_main).
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
 * Every constant below was MEASURED, not guessed.  Request shapes were
 * captured by pointing system CoreFoundation at a .test service
 * (__CFPreferencesTestDaemon) and dumping the traffic; opcodes, keys and
 * service selection were read out of the shipped CoreFoundation binary.
 * Evidence and commands are in cf_cmds/local/NOTES.md, section
 * "MEASURED wire contract".
 *
 * Do not "tidy" these names: they are the wire format.  An earlier revision of
 * this file invented a lower-case key vocabulary ("command", "version",
 * "domain", ...) and string command selectors.  None of that exists.  The
 * real vocabulary is "CFPreferences"-prefixed and the dispatch is an integer
 * opcode switch on CFPreferencesOperation.
 */

#ifndef __CFPrefsXPCProtocol_H__
#define __CFPrefsXPCProtocol_H__

#include <CoreFoundation/CFBase.h>

/* ---------------------------------------------------------------------- *
 * Roles and service selection
 *
 * The role is chosen by getuid(), NOT by argv[1].  The daemon entry point
 * does not parse a role argument.
 *
 *   uid 0    -> the system "daemon" service
 *   uid != 0 -> the per-user "agent" service
 *
 * A client picks the name as follows (decoded at 0x180729e90):
 *
 *   if (__CFProcessIsRestricted())  -> always the production name
 *   else if (getenv(TEST_ENV_VAR))  -> the ".test" name
 *   else                            -> the production name
 *
 * Note the asymmetry: a restricted process can never be redirected to a test
 * service.  That is deliberate and must be preserved.
 * ---------------------------------------------------------------------- */

#define CFPrefsServiceAgent      "com.apple.cfprefsd.agent"
#define CFPrefsServiceDaemon     "com.apple.cfprefsd.daemon"
#define CFPrefsServiceAgentTest  "com.apple.cfprefsd.agent.test"
#define CFPrefsServiceDaemonTest "com.apple.cfprefsd.daemon.test"

/* Present in the binary; used by the daemon for privilege separation. */
#define CFPrefsServiceRead       "com.apple.cfprefsd.read"
#define CFPrefsServiceReadWrite  "com.apple.cfprefsd.read-write"

/* Setting this in the environment redirects clients to the ".test" services. */
#define CFPrefsTestDaemonEnvVar  "__CFPreferencesTestDaemon"

/* Setting this forces clients to bypass the daemon entirely. */
#define CFPrefsAvoidDaemonEnvVar "__CFPREFERENCES_AVOID_DAEMON"

typedef enum {
    CFPrefsRoleDaemon = 0,    /* system-wide; getuid() == 0 */
    CFPrefsRoleAgent  = 1     /* per-user; getuid() != 0    */
} CFPrefsRole;

/* Selecting a service name is a four-line decision and is implemented locally by
 * each side (the daemon in CFXPreferencesDaemon.c, the client in
 * CFPrefsXPCClient.c).  It is deliberately not shared behind a function: the
 * "restricted process ignores the test override" rule is the kind of thing that
 * is dangerous to abstract until both sides have been seen agreeing on it. */

/* ---------------------------------------------------------------------- *
 * Operations
 *
 * The command selector is the int64 value of "CFPreferencesOperation".  There
 * is no separate command string.
 * ---------------------------------------------------------------------- */

typedef enum {
    CFPrefsOpValue          = 1,    /* per-key: set, copy-value, remove.
                                      "Key" and "Value" are present only when
                                      the operation targets a single key.  A
                                      removal is a Value that is absent. */
    CFPrefsOpUnknown3       = 3,    /* observed; semantics not yet characterised */
    CFPrefsOpDomainWide     = 4,    /* whole domain: "CFPreferencesDomain".
                                      Carries no Key. */
    CFPrefsOpBatch          = 5,    /* "CFPreferencesMessages" holds an array
                                      of nested request dictionaries. */
    CFPrefsOpUserSync       = 6,    /* "CFPreferencesUser" only; synchronize. */
    CFPrefsOpSpecial        = 9,    /* handled before any other key is read. */
    CFPrefsOpObserver       = 999   /* add/remove observer; takes a parameter. */
} CFPrefsOperation;

/* ---------------------------------------------------------------------- *
 * Request keys
 * ---------------------------------------------------------------------- */

#define CFPrefsKeyOperation       "CFPreferencesOperation"             /* int64  */
#define CFPrefsKeyUser            "CFPreferencesUser"                  /* string */
#define CFPrefsKeyDomain          "CFPreferencesDomain"                /* string */
#define CFPrefsKeyIsByHost        "CFPreferencesIsByHost"              /* bool   */
#define CFPrefsKeyHostBundleID    "CFPreferencesHostBundleIdentifier"  /* string */
#define CFPrefsKeyKey             "Key"                                /* string */
#define CFPrefsKeyValue           "Value"

/* Context keys that ride along with a value operation. */
#define CFPrefsKeyContainer       "CFPreferencesContainer"
#define CFPrefsKeyIsManaged       "CFPreferencesIsManaged"
#define CFPrefsKeyCurrentAppDomain "CFPreferencesCurrentApplicationDomain"
#define CFPrefsKeyProtectionClass "CFPreferencesFileProtectionClass"   /* int64  */
#define CFPrefsKeyUseCorrectOwner "CFPreferencesUseCorrectOwner"       /* bool   */
#define CFPrefsKeyWriteSync       "CFPreferencesShouldWriteSynchronously"
#define CFPrefsKeyAvoidCache      "CFPreferencesAvoidCache"            /* bool   */

/* Read by the daemon to authorise the caller's identity.  AuditToken must be
 * exactly 32 bytes (sizeof(audit_token_t)); if it is absent or the wrong size
 * the daemon falls back to xpc_connection_get_audit_token() on the peer
 * connection, which is also reachable via a literal "connection" key holding an
 * XPC_TYPE_CONNECTION value. */
#define CFPrefsKeyAuditToken      "CFPreferencesAuditToken"            /* data, 32 bytes */
#define CFPrefsKeyConnection      "connection"
#define CFPrefsKeyAuditTokenImpersonate "CFPreferencesAuditTokenToImpersonate"
#define CFPrefsKeyAccessToken     "CFPreferencesAccessToken"
#define CFPrefsKeyRestrictedRead  "CFPreferencesRestrictedReadability" /* bool */

/* Shared-memory fast path.  When the daemon answers through shmem it reports
 * the region state and the client reads the payload out of band. */
#define CFPrefsKeyShmemName       "CFPreferencesShmemName"
#define CFPrefsKeyShmemIndex      "CFPreferencesShmemIndex"
#define CFPrefsKeyShmemState      "CFPreferencesShmemState"            /* uint64 */

/* Batching. */
#define CFPrefsKeyMessages        "CFPreferencesMessages"              /* array of request dicts */

/* Path reported back when the daemon resolved a domain to a non-canonical
 * location.  The daemon reads it as either a string or an array. */
#define CFPrefsKeyUncanonicalPath "CFPreferencesUncanonicalizedPath"

/* ---------------------------------------------------------------------- *
 * Reply keys
 * ---------------------------------------------------------------------- */

#define CFPrefsReplyPropertyList  "CFPreferencesPropertyList"   /* the value(s) */
#define CFPrefsReplyErrorType     "CFPreferencesErrorType"      /* int64 */
#define CFPrefsReplyErrorDesc     "CFPreferencesErrorDescription" /* string */
#define CFPrefsReplyClientFault   "CFPreferencesErrorClientFault" /* bool */
#define CFPrefsReplyShmemState    CFPrefsKeyShmemState

/* A reply carrying a non-zero CFPreferencesErrorType describes a failure.
 * CFPreferencesErrorClientFault distinguishes "the request was malformed"
 * from "the daemon could not service it". */

/* ---------------------------------------------------------------------- *
 * Value encoding
 *
 * MEASURED: a CFString travels as a native XPC string.  Every other property
 * list type travels as a binary plist inside XPC data.
 *
 * Evidence: setting an int64 was captured as "Value = data (50 bytes)", and a
 * binary plist holding a single integer is exactly 50 bytes beginning with the
 * ASCII "bplist00".
 *
 * The daemon must therefore sniff the XPC type of "Value" rather than always
 * calling a plist decoder.
 * ---------------------------------------------------------------------- */

/* ---------------------------------------------------------------------- *
 * Client routing hooks
 *
 * Defined in CFPrefsXPCClient.c, called from the public entry points in
 * CFPreferences.c.  Each returns 1 when the request was serviced over XPC and
 * has written *out; 0 means "not handled", and the caller falls through to the
 * direct plist path.  A 0 is always safe: routing is best-effort and the direct
 * path is authoritative.
 *
 * __CFPrefsXPCClientShouldRoute() is the gate.  It is false unless
 * __CFPrefsUseXPC=1 is set in the environment, and it is always false inside
 * the daemon (which uses CFPreferences as its own storage backend).
 * ---------------------------------------------------------------------- */

/* Set by __CFXPreferencesDaemon_main so the daemon's storage calls do not
 * recurse back out through XPC.  Must be `extern`: a bare file-scope definition
 * in a header is a tentative definition in C and would be emitted by every
 * translation unit that includes this file. */
CF_PRIVATE extern Boolean __CFPrefsXPCClientBypass;

CF_PRIVATE Boolean __CFPrefsXPCClientShouldRoute(void);

CF_PRIVATE int __CFPrefsXPCClientCopyValue(CFStringRef key, CFStringRef appName,
                                           CFStringRef user, CFStringRef host,
                                           CFTypeRef *out);

CF_PRIVATE int __CFPrefsXPCClientCopyMultiple(CFArrayRef keysToFetch,
                                              CFStringRef appName,
                                              CFStringRef user, CFStringRef host,
                                              CFDictionaryRef *out);

CF_PRIVATE int __CFPrefsXPCClientSetValue(CFStringRef key, CFTypeRef value,
                                          CFStringRef appName,
                                          CFStringRef user, CFStringRef host);

CF_PRIVATE int __CFPrefsXPCClientSetMultiple(CFDictionaryRef keysToSet,
                                             CFArrayRef keysToRemove,
                                             CFStringRef appName,
                                             CFStringRef user, CFStringRef host);

CF_PRIVATE int __CFPrefsXPCClientSynchronize(CFStringRef appName,
                                              CFStringRef user, CFStringRef host,
                                              Boolean *out);

/* ---------------------------------------------------------------------- *
 * Daemon-initiated change notification
 *
 * NOT MEASURED.  This is the one part of the protocol that was never observed
 * on the wire: a capture only ever produced client-initiated requests, so the
 * direction the daemon pushes has no evidence behind it.  The keys below are an
 * internal convention shared by our own daemon and client, NOT a claim about
 * the shipped implementation.  Treat this section as provisional.
 * ---------------------------------------------------------------------- */

#define CFPrefsNotifyOperation    "CFPreferencesOperation"   /* int64 */
#define CFPrefsNotifyIsNotification "CFPreferencesIsNotification" /* bool */
#define CFPrefsNotifyDomain       CFPrefsKeyDomain

/* ---------------------------------------------------------------------- *
 * Testing hooks the daemon honours
 * ---------------------------------------------------------------------- */

#define CFPrefsTestSimulateSlowFS  "kCFPreferencesTestingSimulateSlowFilesystem"
#define CFPrefsTestSimulateOutDisk "kCFPreferencesTestingSimulateOutOfDiskSpace"

#endif /* __CFPrefsXPCProtocol_H__ */