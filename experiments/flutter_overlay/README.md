# Flutter overlay (experiment)

The SDK's item lookup screen (search → verdict) built in Flutter, to measure
what Flutter add-to-app would cost against the WebView overlay. The results are
in [ADR 0002](../../docs/adr/0002-webview-vs-flutter.md) and
[footprint.md](../../docs/footprint.md#flutter-instead-of-the-webview).

Not part of the SDK and not built by CI.

## How it is put together

- `lib/` is the screen. It repeats the web flow (`web-ui/src/ui/`): debounced
  search, the result list, the verdict with keep reasons and numbers, error
  notes with a retry. Favourites and sign-in are left out.
- The screen gets its data from the SDK's public headless API
  (`TarkovCompanion.searchItems` / `getRecommendation`) over a `MethodChannel`,
  answered by the host: `host-game/src/withFlutter/`. The WebView overlay talks
  the bridge protocol to the core instead; for size and memory the difference
  is a few kilobytes against the engine's megabytes.
- The host game gets a third flavor, `withFlutter`: `withSdk` with this screen
  in place of the WebView overlay. Gradle adds it only when the AARs below
  exist.
- One Flutter engine per process, started by the game's prewarm or the first
  open, then kept, as Chromium stays loaded after the WebView closes.

## Build and measure

```bash
cd experiments/flutter_overlay
flutter test
flutter build aar --no-profile --split-debug-info=build/debug-info
cd ../..
./gradlew :host-game:assembleWithFlutterRelease :host-game:assembleWithSdkRelease :host-game:assembleBaselineRelease
scripts/measure-overlay-open.sh 7    # time to a working screen, both overlays
scripts/measure-runtime.sh 15        # cold start and memory, all three builds
```

`--split-debug-info` keeps the Dart symbols out of `libapp.so`, as a
production build would (−0.7 MB per ABI).
