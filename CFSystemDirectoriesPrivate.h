/*
 * CFSystemDirectoriesPrivate.h - search-path enumeration entry points.
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
 * Provenance: original to this project.  Derived from the call sites in
 * CFSystemDirectories.c; see the note below.
 *
 * On Mac OS X the search-path enumeration that CF exposes as
 * CFCopySearchPathForDirectoriesInDomains() is not implemented in CF at all.
 * CFSystemDirectories.c is a ForFoundationOnly shim: it re-exports the
 * enumeration as the CF_SYMBOL __CFStartSearchPathEnumeration() /
 * __CFGetNextSearchPathEnumeration(), which Foundation calls, and forwards
 * each one to the System framework implementation below.
 *
 * The System framework half of that contract is declared in a header that
 * belongs to System.framework, not to CoreFoundation.  It is not present in any
 * DarwinSrc source tree, and it is not in either the Internal or the public
 * macOS SDK, so there is no authentic copy to take.  Rather than put a
 * System-owned header name into the CF source tree, the two declarations are
 * restated here under a CF-owned name; this is the only file that needs them,
 * and it is not installed into the shipped framework.  Only the declarations
 * are needed: both symbols are exported by libsystem_coreservices (hence
 * reachable through libSystem), which is why nothing here needs Foundation
 * linked.
 *
 * Signatures are fixed by how CFSystemDirectories.c calls them, so there is no
 * behaviour to invent:
 *
 *     NSStartSearchPathEnumeration  (dir, domainMask) -> enumeration state
 *     NSGetNextSearchPathEnumeration(state, buffer)  -> enumeration state
 *
 * returning 0 ends the enumeration, which is what the caller's while-loop
 * tests.  Note the second takes a bare `char *` buffer of PATH_MAX bytes and
 * knows nothing of CFIndex; the caller owns that bounds check, which is why
 * it is spelled out there.
 */

#ifndef _CF_HEADERS_CFSYSTEMDIRECTORIESPRIVATE_H_
#define _CF_HEADERS_CFSYSTEMDIRECTORIESPRIVATE_H_

#include <CoreFoundation/CFBase.h>
#include <CoreFoundation/ForFoundationOnly.h>

#if DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED

CF_EXTERN_C_BEGIN

/* Begin enumerating the search path for `dir` within `domainMask`.
 * Returns 0 immediately if the combination yields no directories. */
CFIndex NSStartSearchPathEnumeration(CFSearchPathDirectory dir, CFSearchPathDomainMask domainMask);

/* Retrieve the next path in an enumeration started above, writing at most
 * PATH_MAX bytes into `path`.  Returns 0 once the enumeration is exhausted. */
CFIndex NSGetNextSearchPathEnumeration(CFSearchPathEnumerationState state, char *path);

CF_EXTERN_C_END

#endif /* DEPLOYMENT_TARGET_MACOSX || DEPLOYMENT_TARGET_EMBEDDED */

#endif /* _CF_HEADERS_CFSYSTEMDIRECTORIESPRIVATE_H_ */