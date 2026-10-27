#!/usr/bin/env bash
# Cold start and memory of the host game with and without the SDK, on the
# connected device. Complements `./gradlew footprintReport` (size).
#
#   ./gradlew :host-game:assembleWithSdkRelease :host-game:assembleBaselineRelease
#   scripts/measure-runtime.sh [runs]
#
# Emulator numbers are only good for comparing the two builds with each other.
set -euo pipefail

RUNS="${1:-15}"
ADB="${ADB:-adb}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export MSYS_NO_PATHCONV=1
local_path() { if command -v cygpath >/dev/null; then cygpath -w "$1"; else echo "$1"; fi; }

median() { sort -n | awk '{a[NR]=$1} END {print (NR%2 ? a[(NR+1)/2] : (a[NR/2]+a[NR/2+1])/2)}'; }

pss_kb() { "$ADB" shell dumpsys meminfo "$1" | awk '/TOTAL PSS:/ {print $3; exit} /^ *TOTAL / {print $2; exit}'; }

measure() {
  local flavor=$1 pkg=$2
  "$ADB" install -r "$(local_path "$ROOT/host-game/build/outputs/apk/$flavor/release/host-game-$flavor-release.apk")" >/dev/null
  local times=()
  for _ in $(seq "$RUNS"); do
    "$ADB" shell am force-stop "$pkg"
    sleep 1
    times+=("$("$ADB" shell am start -W -n "$pkg/com.tarkovcompanion.hostgame.GameActivity" | awk '/TotalTime/ {print $2}')")
  done
  local start
  start=$(printf '%s\n' "${times[@]}" | median)
  sleep 3
  local idle
  idle=$(pss_kb "$pkg")
  local overlay="n/a"
  if [[ "$flavor" == "withSdk" ]]; then
    # Tap the in-game Companion button (bottom-right) and let the WebView settle.
    read -r w h < <("$ADB" shell wm size | awk -F'[ x]' '/Physical/ {print $3, $4}')
    if (( w < h )); then t=$w; w=$h; h=$t; fi
    local s=$(( (w < h ? w : h) * 16 / 100 )) m=$(( (w < h ? w : h) * 16 * 35 / 10000 ))
    "$ADB" shell input tap $(( w - m - s / 2 )) $(( h - m - s / 2 ))
    sleep 4
    overlay=$(pss_kb "$pkg")
  fi
  echo "| $flavor | $start ms | $(( idle / 1024 )) MB | $([[ $overlay == n/a ]] && echo n/a || echo "$(( overlay / 1024 )) MB") |"
}

echo "| Build | Cold start (median of $RUNS) | PSS in game | PSS with overlay open |"
echo "|---|---:|---:|---:|"
measure baseline com.tarkovcompanion.hostgame.baseline
measure withSdk com.tarkovcompanion.hostgame
