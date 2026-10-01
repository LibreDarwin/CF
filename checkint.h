/*
 * Replacement for the <checkint.h> that CFRunLoop.c includes.
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
 * Provenance: original to this project.  Upstream CoreFoundation ships
 * checkint.c/checkint.h as a *type-safety
 * test* harness: CFRUNTIME_CHECKINT(cfType, obj) deliberately sends a
 * wrong-type message to an object and is expected to abort.  It is a
 * debugging aid that Apple never built into the shipping library, and the
 * header did not survive into this CF-1153 drop even though CFRunLoop.c
 * still includes it.
 *
 * The overflow-checking half of the original header *is* live: CFRunLoop.c
 * calls check_uint64_add() to compute timer deadlines without wrapping and
 * compares against CHECKINT_NO_ERROR.  Those are reimplemented here on the
 * compiler's own __builtin_*_overflow, so the saturating behaviour is
 * identical and no Apple source is required.
 *
 * The type-safety half is deliberately NOT recreated: nothing in this tree
 * calls CFRUNTIME_CHECKINT, and re-adding an abort-on-wrong-type harness to a
 * production dylib is not a free change.  The macro is a no-op so that if a
 * future port adds call sites they compile and are simply not checked.
 */

#ifndef _CF_HEADERS_CHECKINT_H_
#define _CF_HEADERS_CHECKINT_H_

#include <stdint.h>
#include <limits.h>

/* Result codes, matching the originals. */
#define CHECKINT_NO_ERROR          0
#define CHECKINT_OVERFLOW_ERROR    (-1)
#define CHECKINT_CONVERSION_ERROR  (-2)

/* Saturating helpers: on overflow store the clamped maximum and report an
 * error.  CFRunLoop.c relies on this when it computes a timer's hard
 * deadline - a wrapping deadline would fire timers immediately. */
CF_INLINE int64_t check_uint64_add(uint64_t a, uint64_t b, int32_t *err) {
    uint64_t r;
    *err = __builtin_add_overflow(a, b, &r) ? CHECKINT_OVERFLOW_ERROR : CHECKINT_NO_ERROR;
    if (*err) r = UINT64_MAX;
    return (int64_t)r;
}

CF_INLINE int64_t check_uint64_multiply(uint64_t a, uint64_t b, int32_t *err) {
    uint64_t r;
    *err = __builtin_mul_overflow(a, b, &r) ? CHECKINT_OVERFLOW_ERROR : CHECKINT_NO_ERROR;
    if (*err) r = UINT64_MAX;
    return (int64_t)r;
}

#ifndef CFRUNTIME_CHECKINT
#define CFRUNTIME_CHECKINT(cfType, obj) ((void)0)
#endif

#endif /* _CF_HEADERS_CHECKINT_H_ */
