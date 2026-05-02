#!/usr/bin/env bash
#
# Standalone build script for AirportItlwmShim.kext (a Lilu plugin).
# We build directly with xcrun clang because this is a tiny, single-source
# kext with one external library dependency (Lilu), and a full Xcode project
# would add ~600 lines of pbxproj noise for no benefit.
#
# Output: $OUT_DIR/AirportItlwmShim.kext
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"

OUT_DIR="${OUT_DIR:-$ROOT/build/Build/Products/Debug}"
LILU_HEADERS="${LILU_HEADERS:-$ROOT/Lilu/Lilu/Headers}"
KSDK_HEADERS="${KSDK_HEADERS:-$ROOT/MacKernelSDK/Headers}"
KSDK_LIB="${KSDK_LIB:-$ROOT/MacKernelSDK/Library/x86_64}"
GIT_COMMIT="${GIT_COMMIT:-_local}"

KEXT="$OUT_DIR/AirportItlwmShim.kext"
EXE_DIR="$KEXT/Contents/MacOS"
PRODUCT_NAME="AirportItlwmShim"
MODULE_VERSION="1.0.0"

mkdir -p "$EXE_DIR"
cp "$HERE/Info.plist" "$KEXT/Contents/Info.plist"

# Common kext build flags (cribbed from Lilu's own xcodeproj defaults).
CFLAGS=(
    -arch x86_64
    -target x86_64-apple-macos10.15
    -mkernel
    -fno-builtin -fno-common -fno-stack-protector
    -fno-rtti -fno-exceptions
    -nostdinc -nostdlib
    -ffreestanding
    -fvisibility=hidden -fvisibility-inlines-hidden
    -O2
    -Wall -Wno-unused-parameter
    -DKERNEL -DKERNEL_PRIVATE -D__DARWIN_64_BIT_INO_T=1 -DAPPLE -DNeXT
    -DPRODUCT_NAME="$PRODUCT_NAME"
    -DMODULE_VERSION="$MODULE_VERSION"
    -DLILU_KEXTPATCH_SUPPORT
    -isystem "$KSDK_HEADERS"
    -I "$ROOT"
    -I "$LILU_HEADERS/.."  # so "Headers/plugin_start.hpp" resolves to Lilu/Headers/...
    -I "$LILU_HEADERS"
    -std=gnu++14
    -stdlib=libc++
)

LDFLAGS=(
    -arch x86_64
    -target x86_64-apple-macos10.15
    -nostdlib
    -Xlinker -kext
    -Xlinker -export_dynamic
    -Xlinker -no_uuid
    -L "$KSDK_LIB"
    -lkmod -lkmodc++ -lcc_kext
)

OBJ="$OUT_DIR/AirportItlwmShim.o"
mkdir -p "$(dirname "$OBJ")"

xcrun clang++ "${CFLAGS[@]}" -c "$HERE/AirportItlwmShim.cpp" -o "$OBJ"
xcrun clang++ "${LDFLAGS[@]}" "$OBJ" -o "$EXE_DIR/AirportItlwmShim"

# Ad-hoc codesign — required for kxld even with SIP off.
xcrun codesign -fs - "$KEXT" || true

echo "Built: $KEXT"
nm -arch x86_64 -u "$EXE_DIR/AirportItlwmShim" | head -20 || true
