/*
 * CFCGTypes.h -- the geometry types CoreFoundation publishes.
 *
 * Apple ship this in CoreFoundation.framework and their open-source
 * drop does not carry it; it is one of the seven headers in their
 * framework that the drop lacks.  Foundation's NSGeometry.h includes
 * it, so nothing that reaches <Foundation/Foundation.h> compiles
 * without it.
 *
 * The sizes were measured against Apple's copy rather than recalled:
 * CGFloat is 8 bytes and CGFLOAT_IS_DOUBLE is 1 on this architecture,
 * CGPoint, CGSize and CGVector are 16 and CGRect is 32.  The layouts
 * follow from that -- two CGFloats each, and a CGPoint beside a CGSize
 * -- and are what every caller of this API already assumes.
 *
 * Copyright (c) 2026 Sunneva N. Mariu <sunnevanattsol@gmail.com>
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __COREFOUNDATION_CFCGTYPES__
#define __COREFOUNDATION_CFCGTYPES__

#include <float.h>
#include <stdint.h>
#include <CoreFoundation/CFBase.h>

/*
 * CGFloat is the width of a pointer: double where a pointer is 64 bits
 * and float where it is 32.  CGFLOAT_IS_DOUBLE says which, and callers
 * branch on it.
 */
#if defined(__LP64__) && __LP64__
#define CGFLOAT_TYPE		double
#define CGFLOAT_IS_DOUBLE	1
#define CGFLOAT_MIN		DBL_MIN
#define CGFLOAT_MAX		DBL_MAX
#define CGFLOAT_EPSILON		DBL_EPSILON
#else
#define CGFLOAT_TYPE		float
#define CGFLOAT_IS_DOUBLE	0
#define CGFLOAT_MIN		FLT_MIN
#define CGFLOAT_MAX		FLT_MAX
#define CGFLOAT_EPSILON		FLT_EPSILON
#endif

typedef CGFLOAT_TYPE CGFloat;
#define CGFLOAT_DEFINED		1

CF_ASSUME_NONNULL_BEGIN

/*! A point in a two-dimensional coordinate system. */
struct CGPoint {
	CGFloat x;
	CGFloat y;
};
typedef struct CGPoint CGPoint;

/*! A width and a height. */
struct CGSize {
	CGFloat width;
	CGFloat height;
};
typedef struct CGSize CGSize;

/*! A two-dimensional displacement. */
struct CGVector {
	CGFloat dx;
	CGFloat dy;
};
typedef struct CGVector CGVector;
#define CGVECTOR_DEFINED	1

/*! An origin and a size. */
struct CGRect {
	CGPoint origin;
	CGSize size;
};
typedef struct CGRect CGRect;

/*! The four sides of a rectangle, for the functions that divide one. */
typedef CF_ENUM(uint32_t, CGRectEdge) {
	CGRectMinXEdge = 0,
	CGRectMinYEdge = 1,
	CGRectMaxXEdge = 2,
	CGRectMaxYEdge = 3
};

CF_ASSUME_NONNULL_END

#endif /* __COREFOUNDATION_CFCGTYPES__ */
