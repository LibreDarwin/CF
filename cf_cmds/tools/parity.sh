#!/bin/sh
#
# Copyright (C) 2026, LibreDarwin
# SPDX-License-Identifier: BSD-3-Clause
#
# Differential test for uuidgen against the system copy.  Both programs
# draw fresh random identifiers, so every identifier is masked to a
# canonical placeholder before the outputs are compared; what is actually
# being compared is the shape of the output, the ordering of the bytes
# within it, the argument grammar, and the exit status.
#
# cfprefsd is not compared this way: it is a daemon, it must be run as
# root, and running a second one against the same Mach service would
# displace the system's.  Its parity evidence is the instruction-by-
# instruction comparison against the shipped binary, not this harness.

set -u

UUIDGEN=${1:-build/release/uuidgen}
REFERENCE=${REFERENCE:-/usr/bin/uuidgen}

if [ ! -x "$REFERENCE" ]; then
	echo "SKIP: no reference uuidgen at $REFERENCE" >&2
	exit 77
fi
if [ ! -x "$UUIDGEN" ]; then
	echo "FAIL: $UUIDGEN is not executable" >&2
	exit 1
fi

fail=0
pass=0

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/cf_cmds.XXXXXX") || exit 1
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

# Mask the random parts: the canonical UUID, then each 0xNN byte.
mask() {
	sed -E 's/[0-9A-F]{8}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{4}-[0-9A-F]{12}/UUID/g;
	        s/0x[0-9A-F]{2}/0xNN/g'
}

# Capture masked stdout+stderr and the real exit status of a program,
# because "fail $?" in a pipeline reports the status of the last command
# in the pipe, not the program under test.
capture() {
	outfile=$1
	shift
	"$@" >"$tmpdir/raw" 2>&1
	echo $? >"$tmpdir/rc"
	mask <"$tmpdir/raw" >"$outfile"
}

# Both copies see the same arguments.
both() {
	name=$1
	shift

	capture "$tmpdir/apple" "$REFERENCE" "$@"
	ap=$(cat "$tmpdir/rc")
	capture "$tmpdir/ours" "$UUIDGEN" "$@"
	ou=$(cat "$tmpdir/rc")

	if cmp -s "$tmpdir/apple" "$tmpdir/ours" && [ "$ap" = "$ou" ]; then
		pass=$((pass + 1))
		echo "ok    $name (rc=$ap)"
	else
		fail=$((fail + 1))
		echo "FAIL  $name (rc apple=$ap ours=$ou)"
		diff -u "$tmpdir/apple" "$tmpdir/ours"
	fi
}

both "no arguments"
both "-hdr" -hdr
both "unknown option" -bad
both "bare word" foo
both "two arguments" 1 2
both "-hdr twice" -hdr -hdr
both "empty argument" ""
both "-n is not a flag" -n 1
both "case sensitivity" -HDR
both "leading dash" -
both "long option" --hdr
both "three arguments" -hdr -hdr -hdr

# CFUUIDVersionNumber selects the UUID version.  Only the value 1 means
# anything; anything unparseable must fall back to version 4.  Base-0
# parsing means 0x1 and 01 also count as 1.
for value in 1 0 2 abc '' 0x1 01 +1 1x; do
	apple_ver=$(CFUUIDVersionNumber="$value" "$REFERENCE" | cut -c15)
	ours_ver=$(CFUUIDVersionNumber="$value" "$UUIDGEN" | cut -c15)
	if [ "$apple_ver" = "$ours_ver" ]; then
		pass=$((pass + 1))
		echo "ok    CFUUIDVersionNumber='$value' -> version $apple_ver"
	else
		fail=$((fail + 1))
		echo "FAIL  CFUUIDVersionNumber='$value' -> apple $apple_ver ours $ours_ver"
	fi
done

# Structure of the -hdr output: three lines, correct prefix, 16 bytes, and
# the 16 bytes must be the unparsed UUID's hex digits with the dashes
# dropped.  This is the part most likely to rot, so check it directly
# against our own output rather than the system's.
hdr=$("$UUIDGEN" -hdr)
line2=$(printf '%s\n' "$hdr" | sed -n 2p)
line3=$(printf '%s\n' "$hdr" | sed -n 3p)
lines=$(printf '%s\n' "$hdr" | wc -l | tr -d ' ')
uuid=$(printf '%s\n' "$hdr" | sed -n '1s|^// ||p')
bytecount=$(printf '%s\n' "$line3" | grep -oE '0x[0-9A-F]{2}' | wc -l | tr -d ' ')
expected=$(printf '%s' "$uuid" | tr -d '-' | sed -E 's/(..)/0x\1\n/g' | sed 's/$//')
actual=$(printf '%s\n' "$line3" | grep -oE '0x[0-9A-F]{2}' | sed 's/$//')

verify() {
	name=$1
	expr=$2
	if eval "$expr"; then
		pass=$((pass + 1))
		echo "ok    -hdr $name"
	else
		fail=$((fail + 1))
		echo "FAIL  -hdr $name"
	fi
}

verify "line count is 3"          '[ "$lines" = 3 ]'
verify "first line is a comment"  'case "$uuid" in ????????-????-????-????-????????????) ;; *) false ;; esac'
verify "warning line is verbatim" "[ \"\$line2\" = '#warning Change the macro name MYUUID below to something useful!' ]"
verify "macro defines MYUUID"     'printf "%s" "$line3" | grep -q "^#define MYUUID CFUUIDGetConstantUUIDWithBytes(kCFAllocatorSystemDefault, "'
verify "macro has 16 bytes"       '[ "$bytecount" = 16 ]'
verify "bytes are the uuid digits" '[ "$actual" = "$expected" ]'

# Upper case, and the canonical width: 36 characters.
width=$(printf '%s' "$uuid" | wc -c | tr -d ' ')
verify "uuid is 36 characters"    '[ "$width" = 36 ]'
verify "uuid is upper case"       '! printf "%s" "$uuid" | grep -q "[a-f]"'

# Uniqueness: two consecutive identifiers must differ.
a=$("$UUIDGEN")
b=$("$UUIDGEN")
if [ "$a" != "$b" ]; then
	pass=$((pass + 1))
	echo "ok    consecutive identifiers differ"
else
	fail=$((fail + 1))
	echo "FAIL  two calls returned the same identifier"
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
