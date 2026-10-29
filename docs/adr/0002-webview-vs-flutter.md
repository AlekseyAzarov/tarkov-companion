# ADR 0002: WebView for the UI flow, not Flutter or native views

**Status:** accepted · 2026-10

## Context

The SDK shows one user flow (search → verdict) inside someone else's game. The UI should be cheap to change and ideally shippable without a game update. Candidates: native Android views, Flutter (add-to-app), a WebView.

## Measured

The WebView and Flutter columns are measurements of the same screen
([`experiments/flutter_overlay`](../../experiments/flutter_overlay/README.md)) in the same sample game:
release builds, emulator API 36 (x86_64), 2026-10-29. Details and the
commands to reproduce them: [footprint.md](../footprint.md#flutter-instead-of-the-webview).

| | WebView (this repo) | Flutter add-to-app | Native views |
|---|---|---|---|
| Added per arm64 device | **13 KB** web assets (+ 0.37 MB SDK code) | **15.3 MB** installed, 7.0 MB download: engine 11.2 MB, Dart code 3.8 MB | ~0 |
| Memory with the UI open, whole device | **≈ +140 MB**: +73 MB in the game, ~70 MB in WebView's renderer process | **≈ +55 MB**, all in the game's process | small |
| Memory after the UI closes | all of it stays for the session | all of it stays (the engine is kept) | |
| First open in the process | 488 ms, 365 ms after prewarm | 856 ms, 820 ms after prewarm (~300 ms of it Impeller on the emulator's GL) | fastest |
| Later opens | 126–158 ms | 124–127 ms | |
| Iteration | `npm run dev` with a mock bridge, hot reload in a desktop browser | hot reload, needs a device/emulator | rebuild + install |
| Ship UX without a game update | possible (remote bundle; protocol already versioned for it) | no (AOT-compiled Dart; no supported code push) | no |
| iOS reuse | same bundle in WKWebView | same Dart code | rewrite |

Before these measurements the table had the WebView at +68 MB (the game's
process only) and Flutter at a typical published 30–50 MB. Counting the
renderer process turned memory from the WebView's strength into its weakness:
it costs about 2.5 times Flutter's memory. Size went the other way: Flutter
adds about 40 times the whole SDK to every device.

## Decision

WebView, with:
- assets served from `https://appassets.androidplatform.net` via `shouldInterceptRequest` (real origin, strict CSP, no file access)
- a versioned `MessageChannel` protocol instead of `addJavascriptInterface` ([ADR 0004](0004-web-message-channel.md))
- graceful handling of the WebView's failure modes: provider missing/updating (`companion_launch_failed`), renderer crash (`onRenderProcessGone` → close overlay, the game survives)

## Consequences

- The memory cost is real, paid on first open and kept for the session: about 140 MB across the game and the renderer process. For low-memory devices the host can ignore the overlay and use the headless API (`searchItems` / `getRecommendation`) with its own UI, as the Flutter experiment does.
- If memory becomes the binding constraint, Flutter is the measured alternative at about 55 MB, paid for with about 7 MB of download per device.
- Behaviour depends on the device's WebView version. The bundle targets Chrome 80 and avoids `Intl` for formatting.
- The web side has its own test suite (vitest) and type-checked protocol validators.
