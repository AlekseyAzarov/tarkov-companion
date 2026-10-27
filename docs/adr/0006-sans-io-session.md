# ADR 0006: tarkov.dev logic in a sans-IO core session

**Status:** accepted · 2026-10 · refines [ADR 0001](0001-cpp-core.md)

## Context

Besides parsing and verdicts, the SDK decides which GraphQL query to send,
how to read an HTTP status, when to fall back to the offline cache, how old
cached data may be and when to reload hideout data. Written per platform,
these rules would be rewritten for iOS and drift.

The core can't do I/O itself: the host owns the network (its proxy,
certificate pinning and HTTP client, see `HttpTransport`), and each platform
has its own cache directory and threads.

## Decision

The core exposes one surface, a session (`tarkov_companion_session_*` in
[`tarkov_companion.h`](../../core/include/tarkov_companion/tarkov_companion.h)),
written as a sans-IO state machine:

- the platform sends **commands**: `search`, `recommend`, `cancel`, `set_profile`;
- the session queues **actions**: `http`, `cache_read`, `cache_write`, `result`, `log`;
- the platform executes each action and reports a **completion**
  (`complete_http`, `complete_cache_read`), which may queue more actions.

Multi-step flows (network, else cache; hideout before verdict) are
continuations inside the session, under one mutex. The session never calls
into the platform, so there is no re-entrancy across the C boundary. The
platform layer is a generic executor
([`SessionDriver.kt`](../../sdk-android/src/main/kotlin/com/tarkovcompanion/sdk/internal/session/SessionDriver.kt))
that knows nothing about tarkov.dev. The engine is an internal class; the C
API has no other surface.

### Settings are JSON documents

Config and profile are set once per session, so parsing cost doesn't matter.
Strings and optional fields need no ownership rules, a new setting doesn't
change the JNI layer, and a settings document can be logged whole.

To keep the checks JSON loses:

- key names, limits and defaults live in
  [`contracts/config-limits.json`](../../contracts/config-limits.json) and are
  generated into C++ and Kotlin; `CoreSettingsTest` checks that the SDK sends
  every key the core knows;
- the core ignores unknown keys and logs each one, so a newer platform layer
  works with an older core.

The Kotlin builders check eagerly, so a mistake fails on the developer's
line without loading the native library. The core checks again and is
authoritative. Both use the same generated limits.

### Bulky data travels as bytes beside the JSON

An action is a small JSON description plus an optional raw payload (request
body, cache value, result document). Completions take bodies as bytes too.
Large responses are never escaped into a JSON string.

### The session owns time

- **Wall clock** for persisted data: cache values are stamped
  `<epoch milliseconds>\n<body>`. The platform's store has no notion of time.
- **Steady clock** for in-memory price expiry.

Both clocks are injectable for tests. A stamp in the future (the clock moved
back) is reported with age 0 and is not treated as fresh.

### Cancel means "no answer needed"

A cancelled request gets no result, even one already queued. An HTTP request
in flight still completes and still fills the offline cache. The cache
fallback is skipped for a cancelled request.

## Consequences

- One implementation of the request, cache and fallback rules, tested
  against a scripted platform under ASan/UBSan
  ([`session_test.cpp`](../../core/tests/session_test.cpp)).
- The JNI layer is a fixed set of functions. New features become new action
  types or settings keys.
- Cost: about 57 KB of the arm64 library ([footprint.md](../footprint.md)).
