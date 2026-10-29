# Footprint

What embedding the SDK costs a game. Reproduce with:

```bash
./gradlew footprintReport                  # size: build/reports/footprint.md (also in every CI run's summary)
scripts/measure-runtime.sh 25              # cold start + memory on a connected device
scripts/measure-overlay-open.sh 7          # time to open the overlay
```

The sample host is built twice, identical except for the SDK (`withSdk` and `baseline` flavors), release mode with R8. The numbers are the difference.

## Size

Measured 2026-10-28, SDK 0.1.0 with the core session, bridge, remote flags, sign-in, favourites and the catalog fallback ([ADR 0006](adr/0006-sans-io-session.md), [0007](adr/0007-bridge-protocol-in-core.md), [0008](adr/0008-remote-flags-and-analytics-catalog.md), [0009](adr/0009-sign-in.md), [0010](adr/0010-favorites-sync.md), [0011](adr/0011-static-catalog.md)), NDK r29, R8 on.

| | |
|---|---:|
| **Per-device download (arm64, App Bundle split)** | **≈ 375 KB** |
| of which native code | 322.0 KB |
| of which Kotlin (dex, after R8) | 39.7 KB |
| of which web UI (HTML/JS/CSS) | 12.8 KB |
| Universal APK, 3 ABIs | +924.7 KB |
| AAR | 592.8 KB |

Inside the arm64 `.so` with the session and the bridge (symbol sizes before packaging; the later parts are in the next table):

| | |
|---|---:|
| JSON parser, recommender, engine, JNI glue | 38 KB |
| Session: request building, cache policy, flows, settings parsing | 22 KB |
| Bridge: protocol parsing and validation, handshake, routing, analytics | 23 KB |
| `std::function` thunks for the session's continuations | 14 KB |
| libc++ / libc++abi pieces we use (strings, maps, mutex, allocation) | 37 KB |
| libunwind (pulled by libc++abi) | 16 KB |

### Cost by part

What each part added to the arm64 library and the dex when it was built:

| Part | Native (arm64) | Dex |
|---|---:|---:|
| Session: requests, cache policy, flows ([ADR 0006](adr/0006-sans-io-session.md)) | +57 KB | ±0 |
| Bridge protocol ([ADR 0007](adr/0007-bridge-protocol-in-core.md)) | +39 KB, 23 KB of it bridge code | −2 KB |
| Remote flags ([ADR 0008](adr/0008-remote-flags-and-analytics-catalog.md)) | +4.4 KB | +0.4 KB |
| Sign-in and SHA-256 ([ADR 0009](adr/0009-sign-in.md)) | +48 KB, 26 KB of it OAuth and SHA-256 | +5 KB |
| Favourites ([ADR 0010](adr/0010-favorites-sync.md)) | +30.4 KB, 17.6 KB of it favourites code | +1.1 KB |
| Catalog fallback ([ADR 0011](adr/0011-static-catalog.md)) | +17.9 KB, 8.6 KB of it catalog code | +0.1 KB |

The rest of each native figure is `std::function` thunks for the session's
continuations and unwind tables. Replacing the continuations with a tagged
struct is the clearest way to win size back, at some cost in readability.

### The demangler

Without the stub, the first arm64 build of the library was **288.5 KB**, and 118 KB of it was libc++abi's Itanium **demangler**, more than all of the SDK's own code. It is reachable only from the default terminate handler, which prints the demangled type of an uncaught exception.

The core is built with `-fno-exceptions` and the C API never lets one escape, so that message can't occur. [`demangle_stub.cpp`](../sdk-android/src/main/cpp/demangle_stub.cpp) defines `__cxa_demangle` as a stub; the linker then never pulls the real one from the static archive. Result: 288.5 → 117.5 KB (−59%) on arm64, universal APK delta 811 → 365 KB. Controlled by the CMake option `TARKOV_COMPANION_STUB_DEMANGLER` (default on). Verified by the instrumented tests running against the stubbed library.

### Other choices that keep it small

- **Static libc++, hidden.** `-DANDROID_STL=c++_static` plus a version script exporting only `JNI_OnLoad` and `--exclude-libs,ALL`. The host's own `libc++_shared.so` (any version) can't clash with ours. ([ADR 0001](adr/0001-cpp-core.md))
- **`RegisterNatives`** instead of `Java_*` exports: one exported symbol.
- **`-Os`, `-ffunction-sections`, `--gc-sections`**, no RTTI, no exceptions.
- **Fields ordered by alignment** where that removes holes on arm64 or armeabi-v7a (checked with clang's `-Wpadded` and `-fdump-record-layouts`). `JsonValue`, the most numerous object, is 40 bytes instead of 48 on 32-bit. The session (1568 bytes on arm64, 832 on 32-bit), sign-in and favourites have no holes, only tail padding. Types whose remaining padding is only a tail or a `std::optional` flag keep their fields grouped by meaning.
- **Own ~500-line JSON parser and writer** instead of nlohmann/json or RapidJSON.
- **No runtime dependencies** besides the Kotlin stdlib ([ADR 0003](adr/0003-zero-runtime-dependencies.md)). The 40 KB of dex is the SDK itself.
- **Preact** for the web flow, with a 25 KB gzip budget enforced by `web-ui/build.mjs` (currently 10.5 KB).
- **16 KB page alignment** (`-z max-page-size=16384`) so the library loads on Android 15+ devices with 16 KB pages.

## Startup

`initialize()` does no I/O and doesn't load the native library. Without a flags URL or sign-in the library loads on the first lookup; with either, on a background thread once the main thread first goes idle, because the remote flags and the stored sign-in live in the core ([ADR 0008](adr/0008-remote-flags-and-analytics-catalog.md)). `initialize()` is wrapped in a `Trace` section, so its exact cost is visible in a Perfetto/systrace capture as `TarkovCompanion.initialize`.

On the emulator (API 36, x86_64), cold start of the host (`am start -W`, `TotalTime`):

| Run | baseline | withSdk |
|---|---:|---:|
| median of 15 | 163 ms | 180 ms |
| median of 25 | 195 ms | 176 ms |

The difference flips sign between runs: it is below the emulator's noise (±20 ms). Conclusion: no measurable effect at this resolution. For a real number, profile on a physical low-end device with Perfetto.

With remote flags (2026-10-27, release builds with a flags URL, which loads the native core at startup):

| Run | baseline | withSdk, no flags URL | baseline | withSdk, flags URL |
|---|---:|---:|---:|---:|
| median of 15 | 234 ms | 269 ms | 243 ms | 319 ms |
| median of 25 | 304 ms | 323 ms | 358 ms | 374 ms |

The baselines alone drifted from 234 to 358 ms between rounds, so these numbers can't separate the cost of the early library load from noise. Deferring that load to the main thread's first idle moment is a precaution, not a measured win. Settling it needs a physical device.

## Opening the overlay

From the tap to the page being ready (its bridge handshake), emulator API 36,
release build, median of 7 (`scripts/measure-overlay-open.sh`):

| | Ready |
|---|---:|
| First open in the process, without `prewarm()` | 488 ms |
| First open after `prewarm()` | 365 ms |
| Later opens | 126–158 ms |

The script measures the game as it ships, which prewarms; the first row comes
from a build that didn't.

Most of the first open is the WebView engine starting in the process.
`TarkovCompanion.prewarm()` starts it ahead of time with
`WebSettings.getDefaultUserAgent()`, which loads Chromium without creating a
WebView. That took 228 ms on the main thread (debug build), and the engine
then stays loaded: the game's PSS goes from 17 MB to 64 MB, and the WebView's
renderer process starts too, at 5 MB until a page loads. That is why
prewarming is the host's call, made when the player is likely to open the
companion, and not something `initialize()` does. The remaining gap to a later
open is the renderer filling up and the first page load.

The page also draws an empty search screen from HTML and CSS alone
(`web-ui/src/index.html`), before its script runs and connects, so the panel
is up a little earlier than the working screen; the script replaces it.

## Memory

PSS, same emulator, release builds (`scripts/measure-runtime.sh`). The WebView
renders in a sandboxed process of its own, so that process is counted next to
the game's. The first row comes from a build of the game that didn't prewarm:

| | baseline | withSdk: game | withSdk: WebView renderer |
|---|---:|---:|---:|
| In game, never prewarmed or opened | 17 MB | 18 MB | none |
| In game, after `prewarm()` | n/a | 64 MB | 5 MB |
| Overlay open | n/a | 90 MB | 69–78 MB |
| Overlay closed again | n/a | 90 MB | 79 MB |

Before first use the SDK costs almost nothing. **Opening the overlay costs
about 140 MB**: +73 MB in the game's process (Chromium) and about 70 MB in the
renderer process. Nothing of it comes back when the overlay closes: the SDK
destroys its WebView in `onDestroy`, but Chromium's shared state stays mapped
and Chromium keeps the renderer process for the next page, so a game with no
other web UI pays this once per session. That is the main price of the
WebView approach, weighed in [ADR 0002](adr/0002-webview-vs-flutter.md).
Earlier versions of this page counted the game's process only (+68 MB).

## Flutter instead of the WebView

The same screen built with Flutter add-to-app
([`experiments/flutter_overlay`](../experiments/flutter_overlay/README.md)),
in a third build of the host, `withFlutter`: `withSdk` with that screen in
place of the WebView overlay. Measured 2026-10-29, Flutter 3.47.8 (Impeller),
release builds with R8, the Dart code built with `--split-debug-info`.

Size, what Flutter adds per arm64 device on top of `withSdk`:

| | Installed | Download (deflate) |
|---|---:|---:|
| Engine, `libflutter.so` | 11.2 MB | 5.2 MB |
| Dart code, `libapp.so` | 3.8 MB | 1.4 MB |
| Embedding (dex, after R8) | 0.2 MB | 0.2 MB |
| Assets (licence notices, shaders) | 0.1 MB | 0.1 MB |
| **Total** | **15.3 MB** | **7.0 MB** |
| For comparison: the whole SDK | 0.37 MB | 0.19 MB |

An empty `MaterialApp` is already 2.4 MB of `libapp.so`; the widgets the
screen uses add the rest. The universal APK with three ABIs grows from 1.9 MB
to 46 MB.

Memory, PSS on the same emulator. Flutter has no second process; its engine
is kept for the process once started, as Chromium is:

| | WebView: game + renderer | Flutter |
|---|---:|---:|
| In game, after prewarm | 64 + 5 MB | 60–68 MB |
| Overlay open | 90 + 69 MB | 62–79 MB |
| Overlay closed again | 90 + 79 MB | 69–70 MB |
| **Cost of the open overlay over baseline** | **≈ +140 MB** | **≈ +55 MB** |

Opening, from the tap to a working screen (the WebView's bridge handshake,
Flutter's first frame), median of 7; the rows without prewarm come from builds
that skipped it:

| | WebView | Flutter |
|---|---:|---:|
| First open, without prewarm | 488 ms | 856 ms |
| First open after prewarm | 365 ms | 820 ms |
| Later opens | 126–158 ms | 124–127 ms |
| Prewarm, main thread | 228 ms | 42–92 ms |

Flutter's engine starts quickly, so prewarming barely helps it: almost all of
its first open is the first frame on a new surface. About 300 ms of that is
Impeller on the emulator's OpenGL ES; with Skia the first frame took
~530 ms. A physical device renders Impeller over Vulkan, so this row needs one
to settle.

Cold start of the game is the same for all three builds (interleaved runs,
medians 205–231 ms): the Flutter engine starts only when the game prewarms
it.
