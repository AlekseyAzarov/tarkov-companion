# ADR 0008: Remote flags in the core, analytics names in a catalog

**Status:** accepted · 2026-10 · builds on [ADR 0007](0007-bridge-protocol-in-core.md)

## Context

Remote flags ([remote-flags.md](../remote-flags.md)) carry rules every
platform must apply identically: a document with an unknown schema is
ignored, a bad push must not brick the SDK, the cached document applies
before the network one, and the kill switch depends on the SDK version. The
bridge's handshake also depends on two flags, which must be the ones in
force when the page connects, not when it opened.

Analytics event names are a compatibility promise
([analytics.md](../analytics.md)), and both the core and the platform send
events.

## Decision

### The core owns the flags

`remoteFlagsUrl` is a session setting. The session reads the last good
document from the cache and applies it, fetches the URL, and caches and
applies a good answer. A document that can't be used is logged and ignored;
the last good one stays in force. The flags load once per process.

The handshake asks the session for `minimumWebProtocol` and
`disabledBundleVersions` at `hello` time.

### Only the kill switch's verdict leaves the core

The core evaluates `webFlowEnabled` and `disabledSdkVersions` against its
own version and reports `{"type":"flags","launchAllowed":false}`. The
platform keeps the verdict in a volatile field: `showItemLookup` answers
synchronously on the main thread and must never wait on the core.

### With flags or sign-in configured, the core starts early

The session opens on a background thread once the main thread first goes
idle after `initialize()`, so the kill switch and the stored sign-in are
known before the first lookup. Without either, the library loads on the
first lookup. The cost is a `dlopen` of about 320 KB off the main thread;
cold start on the emulator could not separate it from noise
([footprint.md](../footprint.md#startup)).

### Analytics names come from a catalog

[`contracts/analytics-events.json`](../../contracts/analytics-events.json)
lists every event and its properties, generated into C++ and Kotlin
constants.

- The bridge transcript test checks each core event and property against
  it, and that every core event in it is sent.
- The platform checks every event at runtime: one outside the catalog is
  delivered but logged as an error.
- Web events (`web_*`) are free-form.

## Consequences

- The Kotlin layer holds the public API, the action executor, the WebView
  and lifecycle events; it knows nothing about tarkov.dev, the protocol or
  the flags.
- The demo host serves its flags from `files/demo-flags.json` in debug
  builds, so the kill switch can be tried with `adb` (`DemoSupport.kt`).
- Cost: 4.4 KB of the arm64 library, 0.4 KB of dex.
