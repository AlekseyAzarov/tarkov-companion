#!/usr/bin/env bash
# Builds the core's libFuzzer targets with the host clang and runs each one,
# seeded from the contract fixtures (docs/fuzzing.md). Crashing inputs land in
# build/fuzz/crashes/.
#
#   scripts/run-fuzzers.sh [seconds per fuzzer, default 60]
set -euo pipefail

FUZZ_SECONDS="${1:-60}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/fuzz"
CORPUS="$BUILD/corpus"
FUZZERS=(json_fuzzer number_fuzzer bridge_message_fuzzer)

cmake -S "$ROOT/core" -B "$BUILD" -DTARKOV_COMPANION_BUILD_FUZZERS=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang++
cmake --build "$BUILD" --parallel

for fuzzer in "${FUZZERS[@]}"; do
  mkdir -p "$CORPUS/$fuzzer"
done
mkdir -p "$BUILD/crashes"

# Real API responses for the parser, the web side of the bridge transcript for
# the bridge, and a few numbers on both sides of the fast path.
cp "$ROOT"/contracts/fixtures/*.json "$CORPUS/json_fuzzer/"
node "$ROOT/scripts/testing/bridge-seeds.mjs" "$ROOT/contracts/fixtures/bridge_transcript.json" "$CORPUS/bridge_message_fuzzer"
seed=0
for number in 0.3 -2.675e-3 498000 1e22 123456789012345678901e-30 9.99989e-321; do
  printf '%s' "$number" > "$CORPUS/number_fuzzer/seed-$seed"
  seed=$((seed + 1))
done

for fuzzer in "${FUZZERS[@]}"; do
  echo "== $fuzzer for ${FUZZ_SECONDS}s"
  "$BUILD/fuzz/$fuzzer" -max_total_time="$FUZZ_SECONDS" -print_final_stats=1 \
    -artifact_prefix="$BUILD/crashes/$fuzzer-" "$CORPUS/$fuzzer"
done
