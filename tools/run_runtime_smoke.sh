#!/bin/sh
# Runtime smoke test for the CF-1153 build.
#
# Linking a program against the framework is NOT sufficient to prove this build
# works: CoreFoundation's install_name is /System/Library/..., so a test binary
# resolves to the *system* CF and silently exercises Apple's code instead of
# ours.  DYLD_FRAMEWORK_PATH is therefore required, and this script asserts the
# CF-Root dylib is the one that actually loaded before trusting any result.
#
#   usage: tools/run_runtime_smoke.sh [path/to/CoreFoundation.framework]

set -e

CF_SRC=$(cd "$(dirname "$0")/.." && pwd)
ROOT=${1:-$(cd "$CF_SRC/../CF-Root" 2>/dev/null && pwd)}

if [ ! -d "$ROOT/CoreFoundation.framework" ]; then
	echo "error: no CoreFoundation.framework under $ROOT - build it first (make)" >&2
	exit 1
fi

BIN=$(mktemp -t cf_runtime_smoke).XXXXXX
trap 'rm -f "$BIN"' EXIT

SDK=${SDKROOT_CF:-/Users/sunneva/xnuports-root/devel/xcode-tools/build/release/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.Internal.sdk}
CLANG=${CC_CF:-/Users/sunneva/xnuports-root/devel/xcode-tools/build/release/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang}

"$CLANG" -arch arm64 -isysroot "$SDK" -F"$ROOT" -framework CoreFoundation \
	"$CF_SRC/tools/cf_runtime_smoke.c" -o "$BIN"

# Confirm we are about to test OUR dylib and not the system one.
loaded=$(DYLD_FRAMEWORK_PATH="$ROOT" DYLD_PRINT_LIBRARIES=1 "$BIN" 2>&1 |
	awk '/CF-Root\/CoreFoundation/ { for (i = 1; i <= NF; i++) if ($i ~ /^\//) { print $i; exit } }')
if [ -z "$loaded" ]; then
	echo "error: CF-Root dylib was not loaded - refusing to report a result that" >&2
	echo "       would actually be testing the system CoreFoundation" >&2
	exit 1
fi
echo "loaded: $loaded"

# The case-mapping/property/plist paths depend on data sections embedded at
# link time, and have crashed non-deterministically before, so repeat the run
# rather than trusting a single pass.
iterations=${ITERATIONS:-25}
i=1
while [ "$i" -le "$iterations" ]; do
	if ! DYLD_FRAMEWORK_PATH="$ROOT" "$BIN" >/dev/null 2>&1; then
		echo "FAIL: iteration $i/$iterations crashed (exit $?)" >&2
		exit 1
	fi
	i=$((i + 1))
done

echo "PASS: $iterations/$iterations iterations clean"