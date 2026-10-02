/*
 * CFFileSecurity.h - file ownership/mode state for a CFURL.
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
 * ----------------------------
 *
 * Provenance: original to this project.  This header is included by CFURLPriv.h
 * (as <CoreFoundation/CFFileSecurity.h>)
 * but is absent from the CF-1153 drop, so the URL sources could not compile
 * without it.  Upstream this is a CoreFoundation *private* header, not a
 * public one, which is why it is not in this drop's shipped header set.
 *
 * Only the opaque type is actually required by this tree: CFURL's private
 * URLState struct carries a CFFileSecurityRef field, and nothing in the
 * drop ever populates it (the three accessor names appear only in
 * comments marking deprecated URL property getters).  So this provides the
 * type and its accessors' declarations without an implementation, which
 * matches the state of the surrounding code instead of inventing a
 * behaviour nothing calls.
 *
 * If CFURL ever starts populating fileSecurity, the accessors need real
 * implementations (a stat(2)-based owner/group/mode reader); see the
 * comment at the fileSecurity field in CFURLPriv.h.
 */

#ifndef _CF_HEADERS_CFFILESECURITY_H_
#define _CF_HEADERS_CFFILESECURITY_H_

#include <CoreFoundation/CFBase.h>

#if TARGET_OS_MAC

typedef const struct __CFFileSecurity *CFFileSecurityRef;

/* Declared for completeness; see the note above - not implemented in this
   tree, and not called by it.  CFFileSecurityGetOwner() and
   CFFileSecurityGetGroup() return a retained CFStringRef, and
   CFFileSecurityGetMode() returns a cf_16-bit file mode. */
CF_PRIVATE CFStringRef CFFileSecurityGetOwner(CFFileSecurityRef security);
CF_PRIVATE CFStringRef CFFileSecurityGetGroup(CFFileSecurityRef security);
CF_PRIVATE UInt16 CFFileSecurityGetMode(CFFileSecurityRef security);

#endif /* TARGET_OS_MAC */

#endif /* _CF_HEADERS_CFFILESECURITY_H_ */
