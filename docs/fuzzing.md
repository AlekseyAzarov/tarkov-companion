# Fuzzing

The core reads two kinds of untrusted input: responses from the network and messages from the page in the WebView. Three libFuzzer targets in [`core/fuzz`](../core/fuzz) cover them, built with ASan and UBSan:

| Target | Input | Checked beyond "no crash, no undefined behaviour" |
|---|---|---|
| `json_fuzzer` | any bytes | a parsed document written back parses again; after that, writing it back no longer changes the text |
| `number_fuzzer` | any bytes that parse as a JSON number | the value against `strtod`: bit for bit for decimals with up to 15 significant digits and a power of ten within ±22, within 4 ε elsewhere between 1e-280 and 1e280 |
| `bridge_message_fuzzer` | any bytes as a page message | the native reply (ok or error, carrying the page's id) is valid JSON |

The writer's first pass can change numbers on purpose: it writes fractions with six significant digits (`%.6g`), so `919976123456.5` becomes `9.19976e+11`, which reads back as a whole number. `json_fuzzer` therefore checks that the text is stable from the second pass on.

## Running

```bash
bash scripts/run-fuzzers.sh 60   # seconds per target; needs clang and Node
```

The script builds into `build/fuzz`, seeds the corpora (the contract fixtures for `json_fuzzer`, the web side of [`bridge_transcript.json`](../contracts/fixtures/bridge_transcript.json) for the bridge, a few numbers on both sides of the fast path) and runs each target in turn. CI runs it for a minute per target and uploads `build/fuzz/crashes/` when one fails.

To reproduce a crash, pass the saved input to the target:

```bash
build/fuzz/fuzz/json_fuzzer build/fuzz/crashes/json_fuzzer-crash-<hash>
```

## On an Android device

The NDK ships libFuzzer, so the targets also run on a device or emulator, with the libc++ the SDK ships. Configure `core` with the NDK toolchain (`-DANDROID_ABI=x86_64 -DANDROID_STL=c++_static -DTARKOV_COMPANION_BUILD_FUZZERS=ON`), push the fuzzer, its corpus and the NDK's `libclang_rt.asan-<arch>-android.so`, and run it with `LD_LIBRARY_PATH` pointing at the runtime. The core is built without RTTI on Android, so `core/fuzz/CMakeLists.txt` turns off UBSan's vptr check there; otherwise it reports `shared_ptr` control blocks as objects with an invalid vptr.

## Numbers

Up to 15 significant digits and within 10^±22, the parser multiplies or divides an exact integer by an exact power of ten, which rounds once and gives the correctly rounded result (Clinger's fast path). All prices and short ratios fall in this range. Longer or more extreme numbers go through `pow` and may be a few ulps off. The parser used to scale every number with `pow`, which turned `0.3` into `0.30000000000000004`; run against that code from an empty corpus, `number_fuzzer` finds it on its own (`3.30` read as 3.3000000000000003).
