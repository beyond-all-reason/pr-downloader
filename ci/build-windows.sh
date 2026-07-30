#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Cross builds the standalone pr-downloader CLI for windows x86_64 and drops the
# result, plus the runtime DLLs it needs, in OUT_DIR. Meant to be run inside the
# image built from ci/Dockerfile.windows-x86_64.
#
# Nothing here runs the produced exe; that has to happen on a windows host or
# under wine.

set -eu

SRC_DIR="${SRC_DIR:-/src}"
BUILD_DIR="${BUILD_DIR:-/build}"
OUT_DIR="${OUT_DIR:-/out}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
TOOLCHAIN_FILE="${TOOLCHAIN_FILE:-$SRC_DIR/ci/toolchain-mingw-w64-x86_64.cmake}"
MINGWLIBS="${MINGWLIBS:?set MINGWLIBS to a mingwlibs64 checkout}"

cmake -S "$SRC_DIR" -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
	-DMINGWLIBS="$MINGWLIBS" \
	-DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# jsoncpp and minizip always come from the vendored copies here: the system
# lookups for both are gated on UNIX in the top level CMakeLists.
cmake --build "$BUILD_DIR" --target \
	pr-downloader pr-base64 pr-jsoncpp pr-downloader_cli

BIN="$BUILD_DIR/src/pr-downloader.exe"

mkdir -p "$OUT_DIR"
cp "$BIN" "$OUT_DIR/pr-downloader.exe"
x86_64-w64-mingw32-objcopy --only-keep-debug "$OUT_DIR/pr-downloader.exe" "$OUT_DIR/pr-downloader.exe.debug"
x86_64-w64-mingw32-objcopy --strip-debug --strip-unneeded "$OUT_DIR/pr-downloader.exe"
x86_64-w64-mingw32-objcopy --add-gnu-debuglink="$OUT_DIR/pr-downloader.exe.debug" "$OUT_DIR/pr-downloader.exe"

# mingwlibs64 ships shared libraries only, so the exe is not self contained.
# Walk the import tables until nothing new turns up, since the DLLs pull in each
# other as well.
pending="$OUT_DIR/pr-downloader.exe"
while [ -n "$pending" ]; do
	next=""
	for f in $pending; do
		for dll in $(x86_64-w64-mingw32-objdump -p "$f" | sed -n 's/.*DLL Name: //p'); do
			if [ -f "$MINGWLIBS/dll/$dll" ] && [ ! -f "$OUT_DIR/$dll" ]; then
				cp "$MINGWLIBS/dll/$dll" "$OUT_DIR/$dll"
				next="$next $OUT_DIR/$dll"
			fi
		done
	done
	pending="$next"
done

echo "== bundled runtime =="
ls -l "$OUT_DIR"
