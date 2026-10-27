#!/usr/bin/env bash
# Cross-compiles the core test suite with the NDK and runs it on a connected
# device or emulator. Useful on machines without a host C++ toolchain, and it
# exercises the exact libc++/ABI the SDK ships with.
#
#   ANDROID_NDK=/path/to/ndk scripts/run-core-tests-on-device.sh [abi]
set -euo pipefail

ABI="${1:-x86_64}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/core-android-$ABI"
: "${ANDROID_NDK:?set ANDROID_NDK to the NDK root}"
ADB="${ADB:-adb}"
NINJA_ARGS=()
if [[ -n "${CMAKE_MAKE_PROGRAM:-}" ]]; then NINJA_ARGS=(-DCMAKE_MAKE_PROGRAM="$CMAKE_MAKE_PROGRAM"); fi

cmake -S "$ROOT/core" -B "$BUILD" -G Ninja "${NINJA_ARGS[@]}" \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_static \
  -DTARKOV_COMPANION_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD"

REMOTE=/data/local/tmp/tarkov_companion_core_tests
# Git Bash on Windows: stop MSYS from rewriting device paths, and hand adb
# native local paths instead.
export MSYS_NO_PATHCONV=1
local_path() { if command -v cygpath >/dev/null; then cygpath -w "$1"; else echo "$1"; fi; }
"$ADB" shell "rm -rf $REMOTE && mkdir -p $REMOTE/fixtures"
"$ADB" push "$(local_path "$BUILD/tests/tarkov_companion_core_tests")" "$REMOTE/" >/dev/null
for f in "$ROOT"/contracts/fixtures/*.json; do
  "$ADB" push "$(local_path "$f")" "$REMOTE/fixtures/" >/dev/null
done
# Read as fixtures/../analytics-events.json, like in the repository.
"$ADB" push "$(local_path "$ROOT/contracts/analytics-events.json")" "$REMOTE/" >/dev/null
# The binary has the host fixtures path baked in; point it at the device copy.
"$ADB" shell "cd $REMOTE && chmod +x tarkov_companion_core_tests && \
  TARKOV_COMPANION_FIXTURES_DIR=$REMOTE/fixtures ./tarkov_companion_core_tests --gtest_color=yes"
