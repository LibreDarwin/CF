
include MakefileVersion

MIN_MACOSX_VERSION=11.0
MAX_MACOSX_VERSION=110000

# CF-1153 targeted Mac OS X 10.10 and Apple's private headers.  Neither ships in
# any public SDK, so the missing non-CF headers are taken from their real source
# trees in this checkout rather than from a hand-written "Headers" repo.
#
# Include roots:
#   $(OBJBASE)      - the build tree.  The staging rule below copies every CF
#                     header into $(OBJBASE)/CoreFoundation, which is what makes
#                     the <CoreFoundation/Foo.h> convention resolve.
#   $(XMAP)         - a generated mapping directory (see the xmap rule below).
#                     It exposes libdispatch's private headers under the
#                     "dispatch/" prefix the sources actually include
#                     (<dispatch/private.h>), and liblaunch's bootstrap.h under
#                     the "servers/" prefix that bootstrap_priv.h includes.
#   $(TREE_INCLUDES)- the DarwinSrc trees themselves, for the remaining private
#                     headers: launchd, libpthread, cctools, libdispatch, ICU, xnu.
#
# Two include-path traps, both verified to compile clean only when avoided:
#
#  1. Neither "." nor $(OBJBASE)/CoreFoundation may be an include path.  CF-1153
#     ships a Windows/Linux-oriented TargetConditionals.h that defines
#     TARGET_OS_LINUX 1 and TARGET_OS_MAC 0; either path shadows the SDK's copy
#     and silently turns off every macOS branch (dispatch_time_t,
#     CFMessagePortCallBackEx, ...).  Unqualified private includes ("checkint.h",
#     "CFPriv.h") still resolve relative to the including source file, and
#     CoreFoundation_Prefix.h is pulled in with an explicit "./" prefix.
#
#  2. The current libdispatch private headers use xros/bridgeos availability
#     annotations, which the public SDK's Availability.h does not know, so they
#     only compile against the Internal SDK.
#
# CoreFoundation's own headers - checkint.h, CFFileSecurity.h - live here in the
# CF source tree, not in any of the trees above.
SDKROOT_CF ?= /Users/sunneva/xnuports-root/devel/xcode-tools/build/release/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.Internal.sdk
ICU_PREFIX ?= /opt/homebrew/opt/icu4c
ICU_INCLUDE = $(ICU_PREFIX)/include
ICU_LIB = $(ICU_PREFIX)/lib

# Absolute, because it is consumed both as a make prerequisite (relative to this
# directory) and as a symlink target (relative to $(XMAP) two levels deeper).
DARWIN_ROOT ?= $(abspath ../../..)
TREE_LAUNCHD = $(DARWIN_ROOT)/CoreOS/Sources/launchd
TREE_LIBDISPATCH = $(DARWIN_ROOT)/CoreOS/Sources/libdispatch
TREE_LIBPTHREAD = $(DARWIN_ROOT)/CoreOS/Sources/libpthread
TREE_CCTOOLS = $(DARWIN_ROOT)/DeveloperTools/Sources/cctools
TREE_ICU = $(DARWIN_ROOT)/CoreOS/Sources/ICU/icu/icu4c/source
TREE_XNU = $(DARWIN_ROOT)/Kernel/xnu
TREE_LIBC = $(DARWIN_ROOT)/CoreOS/Sources/Libc
# The Internal SDK omits <notify.h> entirely (notify_register_check and
# notify_set_state, which CFUtilities.c needs, live there); Libnotify is its
# source tree.
TREE_LIBNOTIFY = $(DARWIN_ROOT)/CoreOS/Sources/Libnotify
XMAP = $(OBJBASE)/xmap

TREE_INCLUDES = -I$(TREE_LAUNCHD)/liblaunch -I$(TREE_LIBPTHREAD)/private -I$(XMAP) -I$(TREE_LIBDISPATCH) -I$(TREE_ICU)/common -I$(TREE_ICU)/i18n -I$(TREE_XNU)/bsd -I$(TREE_XNU)/libsyscall -I$(TREE_LIBC)/stdtime/FreeBSD -I$(TREE_LIBNOTIFY)

OBJECTS = $(patsubst %.c,%.o,$(wildcard *.c))
OBJECTS += CFBasicHash.o
HFILES = $(wildcard *.h)
INTERMEDIATE_HFILES = $(addprefix $(OBJBASE)/CoreFoundation/,$(HFILES))

PUBLIC_HEADERS=CFArray.h CFBag.h CFBase.h CFBinaryHeap.h CFBitVector.h CFBundle.h CFByteOrder.h CFCalendar.h CFCharacterSet.h CFData.h CFDate.h CFDateFormatter.h CFDictionary.h CFError.h CFLocale.h CFMessagePort.h CFNumber.h CFNumberFormatter.h CFPlugIn.h CFPlugInCOM.h CFPreferences.h CFPropertyList.h CFRunLoop.h CFSet.h CFSocket.h CFStream.h CFString.h CFStringEncodingExt.h CFTimeZone.h CFTree.h CFURL.h CFURLAccess.h CFUUID.h CFUserNotification.h CFXMLNode.h CFXMLParser.h CFAvailability.h CFUtilities.h CoreFoundation.h

PRIVATE_HEADERS=CFBundlePriv.h CFCharacterSetPriv.h CFError_Private.h CFLogUtilities.h CFPriv.h CFRuntime.h CFStorage.h CFStreamAbstract.h CFStreamPriv.h CFStreamInternal.h CFStringDefaultEncoding.h CFStringEncodingConverter.h CFStringEncodingConverterExt.h CFUniChar.h CFUnicodeDecomposition.h CFUnicodePrecomposition.h ForFoundationOnly.h CFBurstTrie.h CFICULogging.h CFFileSecurity.h checkint.h

MACHINE_TYPE := $(shell uname -m)
unicode_data_file_name = $(if $(or $(findstring i386,$(1)),$(findstring i686,$(1)),$(findstring x86_64,$(1))),CFUnicodeData-L.mapping,CFUnicodeData-B.mapping)

OBJBASE_ROOT = CF-Objects
OBJBASE = $(OBJBASE_ROOT)/$(STYLE)
DSTBASE = $(if $(DSTROOT),$(DSTROOT)/System/Library/Frameworks,../CF-Root)

STYLE=normal
STYLE_CFLAGS=-O2
STYLE_LFLAGS=
# The i386 slice in the original -arch i386 -arch x86_64 was dropped: nothing in
# this SDK supports 32-bit, and the toolchain here is arm64.
ARCHFLAGS ?= -arch $(shell uname -m)
INSTALLNAME=/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation_$(STYLE)

CC ?= /Users/sunneva/xnuports-root/devel/xcode-tools/build/release/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang

CFLAGS=-c -x c -pipe -std=gnu99 -Wmost -Wno-trigraphs -Wno-deprecated -isysroot $(SDKROOT_CF) -mmacosx-version-min=$(MIN_MACOSX_VERSION) -fconstant-cfstrings -fexceptions -DCF_BUILDING_CF=1 -DDEPLOYMENT_TARGET_MACOSX=1 -DMAC_OS_X_VERSION_MAX_ALLOWED=$(MAX_MACOSX_VERSION) -DU_SHOW_DRAFT_API=1 -DU_SHOW_CPLUSPLUS_API=0 -I$(OBJBASE) -I$(XMAP) $(TREE_INCLUDES) -I$(ICU_INCLUDE) -DVERSION=$(VERSION) -include ./CoreFoundation_Prefix.h

LFLAGS=-dynamiclib -isysroot $(SDKROOT_CF) -mmacosx-version-min=$(MIN_MACOSX_VERSION) -twolevel_namespace -fexceptions -init ___CFInitialize -compatibility_version 150 -current_version $(VERSION) -Wl,-alias_list,SymbolAliases -sectcreate __UNICODE __csbitmaps CFCharacterSetBitmaps.bitmap -sectcreate __UNICODE __properties CFUniCharPropertyDatabase.data -sectcreate __UNICODE __data $(call unicode_data_file_name,$(MACHINE_TYPE)) -segprot __UNICODE r r

# CF-1153 linked -licucore.A, Apple's private ICU, which no longer ships in the
# SDK.  The locale, calendar, collation, string-transform and encoding work in
# CFLocale.c/CFCalendar.c/CFICUConverters.c and friends is now satisfied by
# upstream ICU; two Apple-only entry points that are still called live in
# shims/unicode/ualoc.h and shims/CFICUDatePatternCompat.h.
ICU_LFLAGS=-L$(ICU_LIB) -licui18n -licuuc


.PHONY: all install clean
.PRECIOUS: $(OBJBASE)/CoreFoundation/%.h

all: install

clean:
	-/bin/rm -rf $(OBJBASE_ROOT)

$(OBJBASE)/CoreFoundation:
	/bin/mkdir -p $(OBJBASE)/CoreFoundation

$(OBJBASE)/CoreFoundation/%.h: %.h $(OBJBASE)/CoreFoundation
	/bin/cp $< $@

# libdispatch's private headers are authored to be *installed* as "dispatch/*.h",
# so <dispatch/private.h> is the correct spelling but the file lives in
# libdispatch/private/private.h.  liblaunch's bootstrap_priv.h has the same
# problem in reverse: it includes <servers/bootstrap.h>, which no SDK carries,
# while launchd's own copy lives at liblaunch/bootstrap.h.  Map both into the
# build tree instead of copying or editing them - the files stay where their
# project owns them.
$(XMAP):
	/bin/mkdir -p $(XMAP)

$(XMAP)/dispatch: $(XMAP)
	/bin/ln -sfn $(TREE_LIBDISPATCH)/private $(XMAP)/dispatch

$(XMAP)/servers: $(XMAP)
	/bin/mkdir -p $(XMAP)/servers

$(XMAP)/servers/bootstrap.h: $(TREE_LAUNCHD)/liblaunch/bootstrap.h $(XMAP)/servers
	/bin/cp $< $@

# Three <mach-o/*> headers (arch.h, getsect.h, ldsyms.h) are simply not shipped
# in any SDK, so they are taken from cctools.  The rest of the family - dyld.h,
# fat.h, loader.h and especially dyld_priv.h - *is* in the SDK, and putting
# cctools/include on the path wholesale would shadow the SDK's newer copies, so
# only the missing three are mapped.
CCTOOLS_MACHO = arch.h getsect.h ldsyms.h
CCTOOLS_MACHO_TARGETS = $(patsubst %,$(XMAP)/mach-o/%,$(CCTOOLS_MACHO))

$(XMAP)/mach-o:
	/bin/mkdir -p $(XMAP)/mach-o

$(XMAP)/mach-o/%.h: $(TREE_CCTOOLS)/include/mach-o/%.h $(XMAP)/mach-o
	/bin/cp $< $@

XMAPS = $(XMAP)/dispatch $(XMAP)/servers/bootstrap.h $(CCTOOLS_MACHO_TARGETS)

$(OBJBASE)/%.o: %.c $(INTERMEDIATE_HFILES) $(XMAPS)
	$(CC) $(STYLE_CFLAGS) $(ARCHFLAGS) $(CFLAGS) $< -o $@

$(OBJBASE)/%.o: %.m $(INTERMEDIATE_HFILES) $(XMAPS)
	$(CC) $(STYLE_CFLAGS) $(ARCHFLAGS) $(CFLAGS) $< -o $@

$(OBJBASE)/CoreFoundation_$(STYLE): $(addprefix $(OBJBASE)/,$(OBJECTS))
	$(CC) $(STYLE_LFLAGS) -install_name $(INSTALLNAME) $(ARCHFLAGS) $(LFLAGS) $^ $(ICU_LFLAGS) -o $(OBJBASE)/CoreFoundation_$(STYLE)

install: $(OBJBASE)/CoreFoundation_$(STYLE)
	/bin/rm -rf $(DSTBASE)/CoreFoundation.framework
	/bin/mkdir -p $(DSTBASE)/CoreFoundation.framework/Versions/A/Resources
	/bin/mkdir -p $(DSTBASE)/CoreFoundation.framework/Versions/A/Headers
	/bin/mkdir -p $(DSTBASE)/CoreFoundation.framework/Versions/A/PrivateHeaders
	/bin/ln -sf A $(DSTBASE)/CoreFoundation.framework/Versions/Current
	/bin/ln -sf Versions/Current/Resources $(DSTBASE)/CoreFoundation.framework/Resources
	/bin/ln -sf Versions/Current/Headers $(DSTBASE)/CoreFoundation.framework/Headers
	/bin/ln -sf Versions/Current/PrivateHeaders $(DSTBASE)/CoreFoundation.framework/PrivateHeaders
	/bin/ln -sf Versions/Current/CoreFoundation $(DSTBASE)/CoreFoundation.framework/CoreFoundation
	/bin/cp Info.plist $(DSTBASE)/CoreFoundation.framework/Versions/A/Resources
	/bin/mkdir -p $(DSTBASE)/CoreFoundation.framework/Versions/A/Resources/en.lproj
	/bin/cp $(PUBLIC_HEADERS) $(DSTBASE)/CoreFoundation.framework/Versions/A/Headers
	/bin/cp $(PRIVATE_HEADERS) $(DSTBASE)/CoreFoundation.framework/Versions/A/PrivateHeaders
	#/usr/bin/strip -S -o $(DSTBASE)/CoreFoundation.framework/Versions/A/CoreFoundation $(OBJBASE)/CoreFoundation_$(STYLE)
	/bin/cp $(OBJBASE)/CoreFoundation_$(STYLE) $(DSTBASE)/CoreFoundation.framework/Versions/A/CoreFoundation
	/usr/bin/dsymutil $(DSTBASE)/CoreFoundation.framework/Versions/A/CoreFoundation -o $(DSTBASE)/CoreFoundation.framework.dSYM
	/usr/sbin/chown -RH -f root:wheel $(DSTBASE)/CoreFoundation.framework
	/bin/chmod -RH a-w,a+rX $(DSTBASE)/CoreFoundation.framework
	/bin/chmod -RH u+w $(DSTBASE)
	install_name_tool -id /System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation $(DSTBASE)/CoreFoundation.framework/Versions/A/CoreFoundation
	@echo "Installing done.  The framework is in $(DSTBASE)"

