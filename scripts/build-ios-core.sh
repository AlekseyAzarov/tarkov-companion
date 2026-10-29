#!/usr/bin/env bash
# The C++ core as sdk-ios/Frameworks/TarkovCompanionCore.xcframework: static
# libraries for iOS devices, the iOS simulator and macOS (where `swift test`
# runs), each with the C header and a module map, so Swift can
# `import TarkovCompanionCore`. Needs macOS with Xcode and CMake.
#
#   scripts/build-ios-core.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/ios-core"
OUTPUT="$ROOT/sdk-ios/Frameworks/TarkovCompanionCore.xcframework"
LIBRARY=libtarkov_companion_core.a
IOS_DEPLOYMENT_TARGET=15.0
MACOS_DEPLOYMENT_TARGET=12.0

# Quiet unless it fails; then the whole output, compiler errors included.
build_slice() {
  local name=$1
  shift
  local log="$BUILD/$name.log"
  mkdir -p "$BUILD"
  # Static libraries only: CMake's checks would otherwise link (and sign) an iOS executable.
  if ! {
    cmake -S "$ROOT/core" -B "$BUILD/$name" -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY "$@" &&
      cmake --build "$BUILD/$name" --parallel
  } > "$log" 2>&1; then
    cat "$log"
    echo "building the core for $name failed" >&2
    exit 1
  fi
}

build_slice ios-device -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$IOS_DEPLOYMENT_TARGET
build_slice ios-simulator -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
  "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=$IOS_DEPLOYMENT_TARGET
build_slice macos "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOS_DEPLOYMENT_TARGET

headers="$BUILD/headers"
rm -rf "$headers"
mkdir -p "$headers"
cp "$ROOT/core/include/tarkov_companion/tarkov_companion.h" "$headers/"
cat > "$headers/module.modulemap" << 'MODULEMAP'
module TarkovCompanionCore {
  header "tarkov_companion.h"
  export *
}
MODULEMAP

rm -rf "$OUTPUT"
xcodebuild -create-xcframework \
  -library "$BUILD/ios-device/$LIBRARY" -headers "$headers" \
  -library "$BUILD/ios-simulator/$LIBRARY" -headers "$headers" \
  -library "$BUILD/macos/$LIBRARY" -headers "$headers" \
  -output "$OUTPUT" > /dev/null

echo "Built $OUTPUT"
for slice in ios-device ios-simulator macos; do
  library="$BUILD/$slice/$LIBRARY"
  echo "- $slice: $(lipo -archs "$library"), static library $(($(stat -f%z "$library") / 1024)) KB"
done

# What the core adds to an app, comparable to the Android .so in docs/footprint.md:
# linked for arm64 devices with dead stripping, only the C API exported. libc++
# is the system's here, while Android links its own.
linked="$BUILD/linked-arm64.dylib"
xcrun --sdk iphoneos clang++ -dynamiclib -arch arm64 -miphoneos-version-min=$IOS_DEPLOYMENT_TARGET \
  -Wl,-force_load,"$BUILD/ios-device/$LIBRARY" -Wl,-exported_symbol,'_tarkov_companion_*' \
  -Wl,-dead_strip -Wl,-x -x c++ /dev/null -o "$linked"
echo "- linked for arm64 devices: $(($(stat -f%z "$linked") / 1024)) KB"
