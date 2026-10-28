# Troubleshooting

Integration issues seen while building the sample host, with symptoms first. If yours isn't here, file it with the [integration issue template](../.github/ISSUE_TEMPLATE/integration-problem.yml) and add the entry once it's solved.

## `NATIVE_UNAVAILABLE` / `UnsatisfiedLinkError`

**Symptom:** every lookup fails with `ErrorCode.NATIVE_UNAVAILABLE`; logcat shows `UnsatisfiedLinkError: ... libtarkovcompanion.so`.

**Causes, most likely first:**

1. Your `abiFilters` include an ABI the SDK doesn't ship (e.g. `x86`), or exclude the one the device needs. Compare `adb shell getprop ro.product.cpu.abi` with both lists.
2. A sideloaded split APK set is missing its ABI split. Install the full set (`bundletool install-apks`).
3. Your R8 config dropped the SDK's consumer rules (custom `proguardFiles` that replace rather than add). `JNI_OnLoad` then can't find `NativeBridge` by name. Symptom in logcat: `JNI_ERR returned from JNI_OnLoad`.

The SDK never loads the library in `initialize()`: it loads it on the first lookup or, with remote flags or sign-in configured, on a background thread once the main thread first goes idle. A missing library is reported as this error code, never as a crash of your startup.

## Calling the SDK from a native thread fails with `ClassNotFoundException`

`env->FindClass("com/...")` on a thread you attached yourself searches the **system** class loader, which doesn't know your app's classes. Use `GetObjectClass(activity)` (as the sample does) or cache a `jclass` global ref from a JNI call that came from Java.

## The game keeps rendering (or draining battery) under the overlay

That is your choice to make ([ADR 0012](adr/0012-overlay-activity.md)): the overlay takes window focus and your activity gets `onPause`, not `onStop`. To stop rendering behind it, pause your loop on `APP_CMD_LOST_FOCUS` / `onWindowFocusChanged(false)`, not just on `onPause`: in multi-window mode `onPause` may not come. The sample's loop blocks in `ALooper_pollOnce(-1)` while unfocused.

## Overlay rotates the game / crashes on Android 8.0

A translucent activity with a fixed `screenOrientation` crashes on API 26 (`Only fullscreen opaque activities can request orientation`) and can rotate the activity below it on other versions. The SDK declares `screenOrientation="behind"`. If you override the SDK's manifest entry, keep that.

## The whole game dies when the overlay crashes

It shouldn't: the SDK handles `onRenderProcessGone`, closes the overlay and reports the analytics event `companion_renderer_gone`. If your game still dies, check whether *your own* WebViews elsewhere lack that override: a renderer crash kills the host process for every WebView without it.

## "Prices may be outdated" banner

The response came from the offline cache (the network failed or tarkov.dev was down) or is older than `priceTimeToLiveSeconds` (default 15 min). The `search_completed` / `verdict_shown` events carry `from_cache`. Persistent? Check whether your `HttpTransport` returns non-2xx for the endpoint.

## `schema_mismatch` errors

tarkov.dev changed its response shape. The SDK keeps serving the last good cached response, so players mostly see stale prices rather than errors. The nightly [contract check](../.github/workflows/contract.yml) opens an issue when this happens; the fix is a query/fixture update and an SDK patch release.

## Verdicts look wrong after a wipe

The flea fee formula (`core/src/flea_fee.h`) follows the community-documented constants, which Battlestate occasionally changes. Cross-check a few items against tarkov.dev's `Item.fleaMarketFee(price:)`; if they diverge, update the constants and `flea_fee_test.cpp`.

## WebView debugging

Debug builds of your app get `chrome://inspect` automatically. Force it on in release with `CompanionConfig.Builder().webContentsDebuggingEnabled(true)`. The SDK only ever turns this process-global switch *on*, never off.

## Developing the web flow without a device

```bash
cd web-ui && npm install && npm run dev   # http://localhost:8000, mock bridge
```
