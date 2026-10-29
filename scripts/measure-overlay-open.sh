#!/usr/bin/env bash
# How long the overlay takes to open, WebView (withSdk) against Flutter
# (withFlutter, experiments/flutter_overlay), on the connected device: from the
# activity start the tap causes to a working screen. For the WebView that is the
# page's bridge handshake, for Flutter the first frame. Each run opens the
# overlay twice: the first open in the process, then a later one. The game
# prewarms the overlay as soon as it has focus, as it ships.
#
#   ./gradlew :host-game:assembleWithSdkRelease :host-game:assembleWithFlutterRelease
#   scripts/measure-overlay-open.sh [runs]
set -euo pipefail

RUNS="${1:-5}"
ADB="${ADB:-adb}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export MSYS_NO_PATHCONV=1
local_path() { if command -v cygpath >/dev/null; then cygpath -w "$1"; else echo "$1"; fi; }

median() { sort -n | awk '{a[NR]=$1} END {print (NR%2 ? a[(NR+1)/2] : (a[NR/2]+a[NR/2+1])/2)}'; }

tap_companion_button() {
  read -r w h < <("$ADB" shell wm size | awk -F'[ x]' '/Physical/ {print $3, $4}')
  local s=$(( (w < h ? w : h) * 16 / 100 )) m=$(( (w < h ? w : h) * 16 * 35 / 10000 ))
  "$ADB" shell input tap $(( w - m - s / 2 )) $(( h - m - s / 2 ))
}

# Milliseconds from the overlay activity's START line to the first line matching $2.
open_milliseconds() {
  local activity=$1 ready_pattern=$2
  "$ADB" logcat -d -v epoch | awk -v activity="$activity" -v ready="$ready_pattern" '
    index($0, "START u0") && index($0, activity) { started = $1 }
    started && $0 ~ ready { printf "%d\n", ($1 - started) * 1000; exit }'
}

measure() {
  local flavor=$1 pkg=$2 activity=$3 ready_pattern=$4
  local first=() later=()

  for _ in $(seq "$RUNS"); do
    "$ADB" shell am force-stop "$pkg"
    sleep 1
    "$ADB" shell am start -W -n "$pkg/com.tarkovcompanion.hostgame.GameActivity" >/dev/null
    # Lets a prewarm finish before the tap.
    sleep 3

    "$ADB" logcat -c
    tap_companion_button
    sleep 4
    first+=("$(open_milliseconds "$activity" "$ready_pattern")")

    "$ADB" shell input keyevent KEYCODE_BACK
    sleep 2
    "$ADB" logcat -c
    tap_companion_button
    sleep 4
    later+=("$(open_milliseconds "$activity" "$ready_pattern")")
  done

  echo "| $flavor | $(printf '%s\n' "${first[@]}" | median) ms | $(printf '%s\n' "${later[@]}" | median) ms | ${first[*]} / ${later[*]} |"
}

install() {
  "$ADB" install -r "$(local_path "$ROOT/host-game/build/outputs/apk/$1/release/host-game-$1-release.apk")" >/dev/null
}

echo "| Build | First open (median of $RUNS) | Later open | All runs, ms (first / later) |"
echo "|---|---:|---:|---|"

install withSdk
measure withSdk com.tarkovcompanion.hostgame CompanionActivity "RaidSim/Analytics: bridge_handshake"

if [[ -d "$ROOT/host-game/build/outputs/apk/withFlutter/release" ]]; then
  install withFlutter
  measure withFlutter com.tarkovcompanion.hostgame.flutter FlutterOverlayActivity "RaidSim/FlutterOverlay: first frame"
fi
