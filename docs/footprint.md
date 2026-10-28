# Footprint

What embedding the SDK costs a game. Reproduce with:

```bash
./gradlew footprintReport                  # size: build/reports/footprint.md (also in every CI run's summary)
scripts/measure-runtime.sh 25              # cold start + memory on a connected device
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

From the tap to the page being ready (connected to native), emulator API 36,
debug build, 4 runs each:

| | WebView created | Ready |
|---|---:|---:|
| First open in the process | ~195 ms | 600–680 ms |
| First open after `prewarm()` | 64–81 ms | 406–490 ms |
| Later opens | 25–46 ms | 165–280 ms |

Most of the first open is the WebView engine starting in the process.
`TarkovCompanion.prewarm()` starts it ahead of time with
`WebSettings.getDefaultUserAgent()`, which loads Chromium without creating a
WebView or a renderer. That took 228 ms on the main thread, and the engine
then stays loaded: the game's PSS went from 40 MB to 87 MB (debug builds; the
same game without the SDK: 38 MB). That is why prewarming is the host's call,
made when the player is likely to open the companion, and not something
`initialize()` does. The remaining gap to a later open is the renderer process
and the first page load.

The page also draws an empty search screen from HTML and CSS alone
(`web-ui/src/index.html`), before its script runs and connects, so the panel
is up a little earlier than the working screen; the script replaces it.

## Memory

PSS of the host process, same emulator:

| | baseline | withSdk |
|---|---:|---:|
| In game, overlay never opened | 16–17 MB | 17 MB |
| Overlay open | n/a | 85 MB |

Before first use the SDK costs almost nothing. **Opening the overlay costs ~68 MB**, almost all of it the WebView (Chromium loaded into the process). That is the main price of the WebView approach, weighed in [ADR 0002](adr/0002-webview-vs-flutter.md). The SDK destroys its WebView in `onDestroy`, but Chromium's shared state stays mapped for the rest of the process, so a game with no other web UI pays this once per session.
