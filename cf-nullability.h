/*
 * cf-nullability.h -- the CF annotation macros the open-source drop
 * does not carry.
 *
 * Appended to the SDK's CFAvailability.h rather than included from
 * anywhere, the same way os-log-impl.h is appended to os/log.h: CF is
 * a submodule and cannot be edited, and every CF header reaches
 * CFAvailability.h through CFBase.h.
 *
 * Apple's open-source CoreFoundation predates the nullability
 * annotations.  Its own headers do not use them, so it builds fine
 * without -- but every other framework header in this SDK expects
 * CoreFoundation to define them.  Security's SecBase.h opens with
 * CF_ASSUME_NONNULL_BEGIN and does not parse without it.  The same
 * header is also where Apple pulls in os/availability.h, so that goes
 * here too.
 *
 * Copyright (c) 2026 Sunneva N. Mariu <sunnevanattsol@gmail.com>
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __XNUPORTS_CF_NULLABILITY_H__
#define __XNUPORTS_CF_NULLABILITY_H__

/*
 * Apple's CFAvailability.h includes this and the drop's does not, which
 * is why nothing that includes only <CoreFoundation/CFBase.h> gets
 * API_AVAILABLE.  Security's SecBase.h does exactly that and then
 * annotates every keychain typedef, so without this the annotations
 * stay unexpanded and each typedef is a syntax error.
 */
#include <os/availability.h>

#if __has_feature(assume_nonnull)
#define CF_ASSUME_NONNULL_BEGIN _Pragma("clang assume_nonnull begin")
#define CF_ASSUME_NONNULL_END   _Pragma("clang assume_nonnull end")
#else
#define CF_ASSUME_NONNULL_BEGIN
#define CF_ASSUME_NONNULL_END
#endif

#if __has_feature(nullability)
#define CF_NULLABLE             _Nullable
#define CF_NONNULL              _Nonnull
#define CF_NULL_UNSPECIFIED     _Null_unspecified
#else
#define CF_NULLABLE
#define CF_NONNULL
#define CF_NULL_UNSPECIFIED
#endif

/*
 * The Swift-facing annotations.  They change nothing for C, but the
 * headers name them, so they have to expand to something.
 */
#if __has_attribute(swift_name)
#define CF_SWIFT_NAME(_name)    __attribute__((swift_name(#_name)))
#else
#define CF_SWIFT_NAME(_name)
#endif

#if __has_feature(attribute_availability_swift)
#define CF_SWIFT_UNAVAILABLE(_msg) \
	__attribute__((availability(swift, unavailable, message=_msg)))
#else
#define CF_SWIFT_UNAVAILABLE(_msg)
#endif

#if __has_attribute(swift_private)
#define CF_REFINED_FOR_SWIFT    __attribute__((swift_private))
#else
#define CF_REFINED_FOR_SWIFT
#endif

/*
 * CF_ENUM and CF_OPTIONS, in Apple's variadic form.
 *
 * The drop defines them as CF_ENUM(_type, _name), taking exactly two
 * arguments.  Apple's takes one or two: the one-argument form declares
 * an anonymous enum with a fixed underlying type, and framework
 * headers use it heavily -- Security's SecBase.h opens its error list
 * with CF_ENUM(OSStatus), which is a hard error against the
 * two-argument definition.  So both spellings are provided here, the
 * drop's definitions being replaced rather than extended.
 */
#undef CF_ENUM
#undef CF_OPTIONS

#define __CF_ENUM_GET_MACRO(_1, _2, NAME, ...) NAME

#if (__cplusplus && __cplusplus >= 201103L && \
     (__has_extension(cxx_strong_enums) || __has_feature(objc_fixed_enum))) || \
    (!__cplusplus && __has_feature(objc_fixed_enum))
#define __CF_NAMED_ENUM(_type, _name)   enum _name : _type _name; enum _name : _type
#define __CF_ANON_ENUM(_type)           enum : _type
#if (__cplusplus)
#define CF_OPTIONS(_type, _name)        _type _name; enum : _type
#else
#define CF_OPTIONS(_type, _name)        enum _name : _type _name; enum _name : _type
#endif
#else
#define __CF_NAMED_ENUM(_type, _name)   _type _name; enum
#define __CF_ANON_ENUM(_type)           enum
#define CF_OPTIONS(_type, _name)        _type _name; enum
#endif

#define CF_ENUM(...) \
	__CF_ENUM_GET_MACRO(__VA_ARGS__, __CF_NAMED_ENUM, __CF_ANON_ENUM)(__VA_ARGS__)

/*
 * The string-enum wrappers.  They are Swift-facing -- they tell the
 * importer to present a CFStringRef typedef as an enum or as a
 * struct-backed extensible enum -- and expand to nothing for C, but
 * Security's SecKey.h names CF_STRING_ENUM on a typedef, so it has to
 * expand to something.
 */
#if __has_attribute(swift_wrapper)
#define CF_STRING_ENUM              __attribute__((swift_wrapper(enum)))
#define CF_EXTENSIBLE_STRING_ENUM   __attribute__((swift_wrapper(struct)))
#else
#define CF_STRING_ENUM
#define CF_EXTENSIBLE_STRING_ENUM
#endif

#define CF_TYPED_ENUM               CF_STRING_ENUM
#define CF_TYPED_EXTENSIBLE_ENUM    CF_EXTENSIBLE_STRING_ENUM

/*
 * The CF_AVAILABLE family.
 *
 * The drop expands these through a table of __NSi_<version> macros
 * that stops at the iOS numbers -- there is no __NSi_10_15 in it -- so
 * CF_AVAILABLE(10_15, 13_0), which Security's SecProtocolTypes.h uses
 * on a typedef, leaves an undefined identifier behind.  Apple long ago
 * replaced that table with a direct expansion to the availability
 * attribute; this is that expansion, for the macOS side, which is the
 * only platform this tree targets.
 */
#undef CF_AVAILABLE
#undef CF_AVAILABLE_MAC
#undef CF_AVAILABLE_IOS
#undef CF_DEPRECATED
#undef CF_DEPRECATED_MAC
#undef CF_DEPRECATED_IOS
#undef CF_ENUM_AVAILABLE
#undef CF_ENUM_AVAILABLE_MAC
#undef CF_ENUM_AVAILABLE_IOS
#undef CF_ENUM_DEPRECATED
#undef CF_ENUM_DEPRECATED_MAC
#undef CF_ENUM_DEPRECATED_IOS

#if __has_attribute(availability)

#define CF_AVAILABLE(_mac, _ios) \
	__attribute__((availability(macosx,introduced=_mac)))
#define CF_AVAILABLE_MAC(_mac) \
	__attribute__((availability(macosx,introduced=_mac)))
#define CF_AVAILABLE_IOS(_ios) \
	__attribute__((availability(macosx,unavailable)))

#define CF_DEPRECATED(_macIntro, _macDep, _iosIntro, _iosDep, ...) \
	__attribute__((availability(macosx,introduced=_macIntro,deprecated=_macDep,message="" __VA_ARGS__)))
#define CF_DEPRECATED_MAC(_macIntro, _macDep, ...) \
	__attribute__((availability(macosx,introduced=_macIntro,deprecated=_macDep,message="" __VA_ARGS__)))
#define CF_DEPRECATED_IOS(_iosIntro, _iosDep, ...) \
	__attribute__((availability(macosx,unavailable)))

#else

#define CF_AVAILABLE(_mac, _ios)
#define CF_AVAILABLE_MAC(_mac)
#define CF_AVAILABLE_IOS(_ios)
#define CF_DEPRECATED(_macIntro, _macDep, _iosIntro, _iosDep, ...)
#define CF_DEPRECATED_MAC(_macIntro, _macDep, ...)
#define CF_DEPRECATED_IOS(_iosIntro, _iosDep, ...)

#endif

/* The enumerator spellings are the same annotations. */
#define CF_ENUM_AVAILABLE(_mac, _ios)       CF_AVAILABLE(_mac, _ios)
#define CF_ENUM_AVAILABLE_MAC(_mac)         CF_AVAILABLE_MAC(_mac)
#define CF_ENUM_AVAILABLE_IOS(_ios)         CF_AVAILABLE_IOS(_ios)
#define CF_ENUM_DEPRECATED(_macIntro, _macDep, _iosIntro, _iosDep, ...) \
	CF_DEPRECATED(_macIntro, _macDep, _iosIntro, _iosDep, __VA_ARGS__)
#define CF_ENUM_DEPRECATED_MAC(_macIntro, _macDep, ...) \
	CF_DEPRECATED_MAC(_macIntro, _macDep, __VA_ARGS__)
#define CF_ENUM_DEPRECATED_IOS(_iosIntro, _iosDep, ...) \
	CF_DEPRECATED_IOS(_iosIntro, _iosDep, __VA_ARGS__)

/*
 * CF_CLOSED_ENUM, which the drop predates.  A closed enum tells the
 * Swift importer the case list is complete; for C it is the ordinary
 * fixed-underlying-type enum with one more attribute.  Security's
 * SecureTransport.h declares SSLSessionState with it.
 */
#if __has_attribute(enum_extensibility)
#define __CF_CLOSED_ENUM_ATTRIBUTES __attribute__((enum_extensibility(closed)))
#else
#define __CF_CLOSED_ENUM_ATTRIBUTES
#endif

#if (__cplusplus && __cplusplus >= 201103L && \
     (__has_extension(cxx_strong_enums) || __has_feature(objc_fixed_enum))) || \
    (!__cplusplus && __has_feature(objc_fixed_enum))
#define CF_CLOSED_ENUM(_type, _name) \
	enum __CF_CLOSED_ENUM_ATTRIBUTES _name : _type _name; enum _name : _type
#else
#define CF_CLOSED_ENUM(_type, _name) _type _name; enum
#endif

/*
 * The underscored spellings.  Foundation's NSObjCRuntime.h defines
 * NS_TYPED_ENUM and NS_TYPED_EXTENSIBLE_ENUM in terms of these, so a
 * header saying NS_TYPED_EXTENSIBLE_ENUM on a typedef -- NSException.h
 * does, on NSExceptionName -- expands to an undefined identifier and
 * does not parse.  They are the same annotations as the CF_ spellings
 * above.
 */
#define _CF_TYPED_ENUM              CF_STRING_ENUM
#define _CF_TYPED_EXTENSIBLE_ENUM   CF_EXTENSIBLE_STRING_ENUM

#endif /* __XNUPORTS_CF_NULLABILITY_H__ */
