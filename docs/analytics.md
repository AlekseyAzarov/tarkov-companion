# Analytics events

Delivered to the host's `AnalyticsSink`; the SDK has no analytics backend of its own. Names and properties are part of the compatibility promise: renaming one is a breaking change.

All property values are strings.

The source of truth is [`contracts/analytics-events.json`](../contracts/analytics-events.json); the table below describes it. The core's tests fail if it sends an event or property not listed there, and the platform logs an error for one at runtime ([ADR 0008](adr/0008-remote-flags-and-analytics-catalog.md)). Add an event there first, then here.

## The product metric

**Time to first verdict**: from the player tapping the companion button to seeing "Keep / Sell on Flea / Sell to trader". Target: under 3 seconds at p75. It's `time_to_first_verdict_milliseconds` on the first `verdict_shown` of a session.

## Events

| Event | When | Properties |
|---|---|---|
| `companion_opened` | overlay created | `restored` (after process death) |
| `companion_closed` | overlay destroyed | `duration_milliseconds` |
| `companion_launch_failed` | overlay couldn't start | `reason`: `webview_unavailable`, or the lowercased `ErrorCode` when the native core can't be reached (e.g. `native_unavailable`) |
| `companion_launch_blocked` | `showItemLookup` refused | `reason`: `remote_kill_switch` |
| `companion_renderer_gone` | WebView renderer crashed or was killed | |
| `bridge_handshake` | web bundle connected | `web_protocol`, `bundle_version`, `result` (`ok` or error code) |
| `bridge_error` | malformed message from the web bundle | `code` |
| `bridge_request_failed` | a search/verdict failed | `code` (lowercased `ErrorCode`) |
| `search_completed` | search answered | `latency_milliseconds`, `from_cache` |
| `verdict_shown` | verdict answered | `verdict`, `latency_milliseconds`, `from_cache`, `time_to_first_verdict_milliseconds` (first only) |
| `sign_in_completed` | a sign-in ended | `result`: `ok`, `cancelled` (page closed or declined) or `failed` |
| `signed_out` | signed out | `reason`: `user`, or `expired` (refresh refused, session revoked) |
| `favorite_toggled` | a favourite was added or removed, from the page or the game | `favorite` (`true` added, `false` removed), `signed_in` |
| `web_*` | sent by the web flow via `track` | free-form, ≤ 10 properties of ≤ 100 characters; e.g. `web_item_opened {source}` |

## Useful queries

- p75 of `time_to_first_verdict_milliseconds`, split by `from_cache`: is the network or the UI slow?
- `bridge_handshake` where `result != ok`, grouped by `bundle_version`: a remotely served bundle out of sync with old SDKs.
- `bridge_request_failed{code=schema_mismatch}` rate: upstream API drift in the wild.
- `companion_renderer_gone` per 1k opens, by device: WebView memory pressure on low-end devices.
