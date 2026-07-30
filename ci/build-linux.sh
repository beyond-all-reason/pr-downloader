#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds the standalone pr-downloader CLI for linux and drops the result in
# OUT_DIR. Meant to be run inside the image built from ci/Dockerfile.linux-x86_64
# but works on any host with the same packages installed.

set -eu

SRC_DIR="${SRC_DIR:-/src}"
BUILD_DIR="${BUILD_DIR:-/build}"
OUT_DIR="${OUT_DIR:-/out}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
RUN_FUNCTIONAL_TESTS="${RUN_FUNCTIONAL_TESTS:-1}"

cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# pr-downloader / prd::base64 / prd::jsoncpp are what the engine links against,
# so build them by name to catch a break there even though the CLI pulls them in.
LIB_TARGETS="pr-downloader pr-base64"
if ninja -C "$BUILD_DIR" -t targets all 2>/dev/null | grep -q '^lib/jsoncpp/libpr-jsoncpp'; then
	LIB_TARGETS="$LIB_TARGETS pr-jsoncpp"
else
	echo "note: jsoncpp came from the system, prd::jsoncpp is an imported target"
fi

cmake --build "$BUILD_DIR" --target $LIB_TARGETS pr-downloader_cli

BIN="$BUILD_DIR/src/pr-downloader"

echo "== $BIN --version =="
"$BIN" --version
echo "== $BIN --help =="
"$BIN" --help
echo "== ldd =="
ldd "$BIN"

if [ "$RUN_FUNCTIONAL_TESTS" = "1" ]; then
	echo "== functional tests =="
	python3 "$SRC_DIR/test/functional_test.py" --pr-downloader-path "$BIN"
fi

mkdir -p "$OUT_DIR"
cp "$BIN" "$OUT_DIR/pr-downloader"
objcopy --only-keep-debug "$OUT_DIR/pr-downloader" "$OUT_DIR/pr-downloader.debug"
objcopy --strip-debug --strip-unneeded "$OUT_DIR/pr-downloader"
objcopy --add-gnu-debuglink="$OUT_DIR/pr-downloader.debug" "$OUT_DIR/pr-downloader"

ls -l "$OUT_DIR"
