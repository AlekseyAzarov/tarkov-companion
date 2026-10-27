# ADR 0002: WebView for the UI flow, not Flutter or native views

**Status:** accepted · 2026-10

## Context

The SDK shows one user flow (search → verdict) inside someone else's game. The UI should be cheap to change and ideally shippable without a game update. Candidates: native Android views, Flutter (add-to-app), a WebView.

## Measured / known

| | WebView (this repo) | Flutter add-to-app | Native views |
|---|---|---|---|
| Added to APK, per device | **≈ 13 KB** web assets (+ SDK code) | ~5–7 MB engine + AOT snapshot per ABI | ~0 |
| Memory with UI open | **+68 MB** (measured, see [footprint.md](../footprint.md)) | ~30–50 MB engine + Dart heap (typical, not measured here) | small |
| Cold open | WebView init, ~0.5 s from `companion_opened` to `bridge_handshake` on the emulator | engine warm-up unless pre-warmed (then memory is paid up front) | fastest |
| Iteration | `npm run dev` with a mock bridge, hot reload in a desktop browser | hot reload, needs a device/emulator | rebuild + install |
| Ship UX without a game update | possible (remote bundle; protocol already versioned for it) | no (AOT-compiled Dart; no supported code push) | no |
| iOS reuse | same bundle in WKWebView | same Dart code | rewrite |

Not measured here: a Flutter build of the same screen. The Flutter figures are typical published numbers, kept separate so they don't read as measurements.

## Decision

WebView, with:
- assets served from `https://appassets.androidplatform.net` via `shouldInterceptRequest` (real origin, strict CSP, no file access)
- a versioned `MessageChannel` protocol instead of `addJavascriptInterface` ([ADR 0004](0004-web-message-channel.md))
- graceful handling of the WebView's failure modes: provider missing/updating (`companion_launch_failed`), renderer crash (`onRenderProcessGone` → close overlay, the game survives)

## Consequences

- The memory cost is real and paid on first open. For low-memory devices the host can ignore the overlay and use the headless API (`searchItems` / `getRecommendation`) with its own UI.
- Behaviour depends on the device's WebView version. The bundle targets Chrome 80 and avoids `Intl` for formatting.
- The web side has its own test suite (vitest) and type-checked protocol validators.
