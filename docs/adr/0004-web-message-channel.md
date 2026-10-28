# ADR 0004: MessageChannel bridge on a real https origin

**Status:** accepted · 2026-10

## Context

The web flow needs to call native code. The common approach, `addJavascriptInterface`, injects a Java object into **every** frame and origin the WebView ever loads, runs its methods on a binder thread, and makes calls synchronous strings with no notion of who called.

## Decision

1. Serve the bundle from `https://appassets.androidplatform.net/companion/` by intercepting requests (`CompanionWebViewClient`). Android reserves that host for this purpose; the page gets a proper https origin, unlike `file://`.
2. After load, native creates a `WebMessageChannel` and posts one port with `postWebMessage(..., targetOrigin)`. The platform only delivers it if the page is on our origin.
3. All traffic flows over that port as JSON envelopes with ids, versions and validation ([bridge-protocol.md](../bridge-protocol.md)).
4. Defence in depth around it:
   - every navigation is refused (`shouldOverrideUrlLoading` → true)
   - requests to any other host get 403
   - CSP: `default-src 'none'; script-src 'self'; connect-src 'none'; img-src 'self'`. The page cannot fetch anything itself.
   - file and content access off; no DOM storage; messages capped at 16 KiB

## Alternatives

- `addJavascriptInterface` with origin checks inside each method: works, but the object is still reachable from any frame, and checking the origin from a binder thread is racy.
- `androidx.webkit`'s `addWebMessageListener`: equivalent guarantees with a nicer API, but adds a dependency ([ADR 0003](0003-zero-runtime-dependencies.md)). Worth switching to if the SDK ever takes AndroidX anyway.

## Consequences

- The page must listen for the port before `onPageFinished`; it does so synchronously at script start (`waitForNativePort`).
- A reload creates a new channel and a new bridge in the core ([ADR 0007](0007-bridge-protocol-in-core.md)); the old bridge is closed and its in-flight work cancelled.
- The web side never knows native types: everything is JSON validated on arrival, which is also what makes a remotely served bundle safe to consider.
