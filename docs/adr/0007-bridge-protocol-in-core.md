# ADR 0007: The WebView bridge protocol runs in the core

**Status:** accepted · 2026-10 · builds on [ADR 0006](0006-sans-io-session.md)

## Context

The bridge protocol to the web UI ([bridge-protocol.md](../bridge-protocol.md))
covers the handshake, version negotiation, validation of untrusted messages,
routing, superseding stale searches and analytics of what the user saw. A
second platform showing the same bundle would need all of it, and two
implementations of a versioned protocol drift in limits, error codes and
edge cases.

## Decision

The protocol runs in the core session (`core/src/bridge_protocol.cpp` for
messages, `core/src/bridge.cpp` for the state machine). The platform layer
carries messages and owns the WebView.

### A bridge is part of the session

Four functions (`open_bridge`, `bridge_receive`, `bridge_event`,
`close_bridge`) and four actions (`bridge_send`, `bridge_state`,
`bridge_close`, `track`) on the same `tarkov_companion_session_*` surface.
A page's requests use the same flows, cache and cancellation as the headless
API. A page reload opens a new bridge. Requests a bridge starts use ids with
the top bit set, which public commands reject, so they can't collide.

### The core reports the bridge's analytics

`bridge_handshake`, `search_completed`, `verdict_shown` with
`time_to_first_verdict_milliseconds`, `bridge_request_failed` and `web_*`
describe what only the bridge knows. They leave the core as `track` actions.
Latencies use the core's steady clock; the platform passes
`openedMillisecondsAgo` once, so the metric covers the whole wait. The
platform keeps the screen-lifecycle events.

### Messages cross the platform layer unread

Untrusted input is validated in one place: the size cap before parsing, the
parser's depth limit for stack use. Character checks are hand-written
(`std::regex` is large and reports errors with exceptions, which the core is
built without).

On Android every call into the core goes through one serial queue
(`SessionDriver`), and the core's actions run on it in order. The bridge is
open before the page's first message is handled; HTTP runs on threads of its
own and never holds a message up.

Replies are correlated by `replyTo` and may arrive in any order.

### A transcript pins the protocol from both sides

[`bridge_transcript.json`](../../contracts/fixtures/bridge_transcript.json)
is the protocol as a conversation: handshake, refusals, unknown types,
limits, search, verdict, errors, track, state, close.

- `core/tests/bridge_transcript_test.cpp` replays it through a real session
  and matches every reply (`"*"` matches any value; objects may have extra
  fields).
- `web-ui/test/transcript.test.ts` checks that the web validators accept
  every reply and that every request type the bundle sends is covered.

## Consequences

- The platform's part of the web UI: create the WebView and its
  `MessageChannel`, serve assets, proxy icons, handle back, save and restore
  state, close the screen.
- If the core can't be reached (missing native library), the overlay closes
  and reports `companion_launch_failed` with the reason.
- Cost: about 39 KB of the arm64 library, 23 KB of it bridge code.
