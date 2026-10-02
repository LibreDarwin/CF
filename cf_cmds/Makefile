# Copyright (C) 2026, LibreDarwin
# SPDX-License-Identifier: BSD-3-Clause
# Clean-room reimplementation of Apple's CoreFoundation commands:
# uuidgen and cfprefsd.
#
# Build layout: every artifact lives under build/; final tools go to
# build/release/ or build/debug/ per CONFIG.
#
# Portable to both GNU make and BSD make (bmake): no pattern rules, no
# ifeq/ifdef/.if conditionals and no $(if)/$(shell) functions.  Per-config
# flags come from make/<CONFIG>.mk so both make variants behave identically.

CONFIG ?= release
SDK    ?= /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
CC     := /Users/sunneva/xnuports-root/devel/xcode-tools/build/release/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang

-include make/$(CONFIG).mk

PREFIX  ?= /usr/local
DESTDIR ?=

BUILD_DIR := build/$(CONFIG)
OBJDIR    := $(BUILD_DIR)/obj

CFLAGS := $(OPT) -std=c11 -D_DARWIN_C_SOURCE -isysroot "$(SDK)" -Wall -Wextra -Wno-deprecated-declarations

# Apple's uuidgen links CoreFoundation and libSystem only, and so does
# Apple's cfprefsd: the daemon itself is CoreFoundation code reached through
# the exported __CFXPreferencesDaemon_main.  No Foundation, no CoreServices.
LFLAGS := -framework CoreFoundation

UUIDGEN      := $(BUILD_DIR)/uuidgen
UUIDGEN_OBJS := $(OBJDIR)/uuidgen.o

CFPREFSD      := $(BUILD_DIR)/cfprefsd
CFPREFSD_OBJS := $(OBJDIR)/cfprefsd.o

all: $(UUIDGEN) $(CFPREFSD)

$(UUIDGEN): $(UUIDGEN_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(UUIDGEN_OBJS) $(LFLAGS)

$(OBJDIR)/uuidgen.o: src/uuidgen/uuidgen.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ src/uuidgen/uuidgen.c

$(CFPREFSD): $(CFPREFSD_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(CFPREFSD_OBJS) $(LFLAGS)

$(OBJDIR)/cfprefsd.o: src/cfprefsd/cfprefsd.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ src/cfprefsd/cfprefsd.c

test: all
	sh tools/parity.sh $(UUIDGEN)

MAN1 := man/uuidgen.1
MAN8 := man/cfprefsd.8

install: all
	install -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/sbin \
		$(DESTDIR)$(PREFIX)/share/man/man1 $(DESTDIR)$(PREFIX)/share/man/man8
	install -m 0755 $(UUIDGEN) $(DESTDIR)$(PREFIX)/bin/uuidgen
	install -m 0755 $(CFPREFSD) $(DESTDIR)$(PREFIX)/sbin/cfprefsd
	install -m 0444 $(MAN1) $(DESTDIR)$(PREFIX)/share/man/man1/uuidgen.1
	install -m 0444 $(MAN8) $(DESTDIR)$(PREFIX)/share/man/man8/cfprefsd.8

man:
	@echo "Rendering $(MAN1) and $(MAN8) is left to mandoc/groff at install time."

clean:
	rm -rf build

.PHONY: all test man install clean
