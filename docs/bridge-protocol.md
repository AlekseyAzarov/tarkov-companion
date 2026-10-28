# Bridge protocol (v1)

How the web flow inside the WebView talks to the native SDK.

Implementations: native, in the C++ core, [`bridge_protocol.cpp`](../core/src/bridge_protocol.cpp) / [`bridge.cpp`](../core/src/bridge.cpp) ([ADR 0007](adr/0007-bridge-protocol-in-core.md)); web [`protocol.ts`](../web-ui/src/bridge/protocol.ts) / [`bridge.ts`](../web-ui/src/bridge/bridge.ts). The platform SDK only carries messages between the page and the core.

Both sides are checked against one conversation, [`bridge_transcript.json`](../contracts/fixtures/bridge_transcript.json): the core replays it and matches every reply; the web validators must accept every reply.

Every name on the wire (fields, request types, events, features, routes, error codes, result payloads) is listed in [`bridge-protocol.json`](../contracts/bridge-protocol.json), [`error-codes.json`](../contracts/error-codes.json) and [`result-payloads.json`](../contracts/result-payloads.json). Both sides generate their constants from these files, so a name can't be misspelt on one side only.

## Why versioning matters here

The web bundle and the native SDK are separate artifacts. Today the bundle ships inside the AAR. Tomorrow it may be served remotely so UX can ship without a game update, and then a player can run **any bundle version against any SDK version**. The protocol is designed for that from day one:

- every message carries the sender's protocol version `version`
- the handshake negotiates compatibility both ways
- unknown message types get an answer (`unsupported_type`) instead of silence, so a newer bundle can feature-detect an older SDK
- unknown fields are ignored on both sides
- both sides validate everything they receive

## Transport

A `MessageChannel` created by native ([`CompanionActivity.connectBridge`](../sdk-android/src/main/kotlin/com/tarkovcompanion/sdk/ui/CompanionActivity.kt)):

1. The page is served from `https://appassets.androidplatform.net/companion/` (see [ADR 0004](adr/0004-web-message-channel.md)).
2. On `onPageFinished`, native posts the string `"tarkov-companion-bridge-port"` with one `MessagePort` via `WebView.postWebMessage(..., targetOrigin = that origin)`. If the page is on another origin, the platform drops the message.
3. The web side accepts the **first** port only and talks over it from then on.

No `addJavascriptInterface`: no Java object is ever exposed to page script.

## Envelope

Web → native (request):

```json
{ "version": 1, "id": "w7", "type": "search", "payload": { "query": "salewa" } }
```

Native → web (response):

```json
{ "version": 1, "replyTo": "w7", "ok": true,  "payload": { ... } }
{ "version": 1, "replyTo": "w7", "ok": false, "error": { "code": "network", "message": "..." } }
```

Native → web (event, unsolicited):

```json
{ "version": 1, "event": "navigate_back", "payload": {} }
```

`id`: 1–32 chars of `[A-Za-z0-9_-]`. `replyTo` is `null` when the request was too malformed to read its id.

## Handshake

The first request must be `hello`. Anything else gets `not_ready`, **except `close`**: a bundle that failed the handshake must still let the player leave.

```json
{ "version": 1, "id": "w1", "type": "hello",
  "payload": { "protocol": 1, "minimumNativeProtocol": 1, "bundleVersion": "0.1.0" } }
```

Native refuses with `incompatible_protocol` if `protocol` < max(native minimum, remote `minimumWebProtocol`) or `minimumNativeProtocol` > native protocol, and with `bundle_disabled` if remote flags list the bundle version. Otherwise:

```json
{ "protocol": 1, "sdkVersion": "0.1.0",
  "features": ["search", "recommend", "track", "state", "close", "icon_proxy", "favorites"],
  "iconProxy": "/companion/icon?url=",
  "initialState": { "route": "item", "query": "gpu", "itemId": "57347ca924597744596b4e71" } }
```

`initialState` is the last `state` the page reported, which the platform saves in `onSaveInstanceState`: after process death the web flow reopens where the player left it.

When the host configured sign-in, `features` also lists `sign_in`, and the payload has `"account": {"signedIn": true, "displayName": "Nikita"}`. That is all the page ever learns about the player: no id, no tokens.

`favorites` is listed by SDKs that support favourites; a bundle should hide the star and the list without it.

## Requests

| type | payload | response payload | limits |
|---|---|---|---|
| `hello` | see above | see above | `bundleVersion` ≤ 32 chars `[0-9A-Za-z.+-]` |
| `search` | `{ query }` | `{ items: ItemSummary[], fromCache, cacheAgeSeconds }` | trimmed query 2–64 chars |
| `recommend` | `{ itemId }` | `{ recommendation, fromCache, hideoutMissing }` | `[A-Za-z0-9_-]{1,64}` |
| `track` | `{ event, properties? }` | `{}` | event `[a-z][a-z0-9_]{0,39}`, ≤ 10 scalar properties, values ≤ 100 chars |
| `state` | `{ route, query?, itemId? }` | `{}` | route ∈ `search`, `item` |
| `close` | `{}` | none (the screen closes) | |
| `sign_in` | `{}` | `{}`: started; the result comes as `account_changed` | only with the `sign_in` feature, else `unsupported_type` |
| `sign_out` | `{}` | `{}` | likewise |
| `favorites_list` | `{}` | `{ items: FavoriteView[] }`, most recently added first | |
| `favorite_set` | `{ itemId, favorite }` | `{}`, after a `favorites_changed` event | `itemId` as for `recommend`; `favorite` boolean. `not_found` if the item wasn't searched or looked at in this session; `invalid_argument` when the list is full |

`FavoriteView` is `{ id, name, shortName, iconLink }`. Native takes the names from its own catalog of items it got from tarkov.dev, never from the page ([ADR 0010](adr/0010-favorites-sync.md)).

`recommendation` is the C++ core's output verbatim. Its shape is pinned by [`contracts/fixtures/core_recommendation.json`](../contracts/fixtures/core_recommendation.json): the core test `MatchesGoldenContract` produces exactly that file, and the web validators are tested against it.

A new `search` cancels the pending one, which is answered with `superseded`.

Messages over 16 KiB are rejected before parsing.

## Events

| event | meaning |
|---|---|
| `navigate_back` | the system back button was pressed while the web flow is on the item screen |
| `account_changed` | `{ account: { signedIn, displayName? } }`: signed in or out, from the page, the game, or an expired session |
| `favorites_changed` | `{ items: FavoriteView[] }`: the whole list after any change (from this page, the game, a sync from another device, or a sign-in or sign-out) |

## Error codes

| code | from | meaning |
|---|---|---|
| `invalid_message` | protocol | malformed envelope or payload |
| `unsupported_type` | protocol | native doesn't know this request type |
| `not_ready` | protocol | request before a successful `hello` |
| `incompatible_protocol` | protocol | version negotiation failed |
| `bundle_disabled` | protocol | this bundle version is switched off remotely |
| `superseded` | protocol | a newer search replaced this one |
| `network`, `api`, `schema_mismatch`, `not_found`, `invalid_argument`, `internal`, `sign_in_cancelled`, `sign_in_failed`, `not_signed_in` | core | why the operation failed ([`error-codes.json`](../contracts/error-codes.json)) |
| `timeout`, `invalid_response`, `no_native_port` | web only | raised locally by the web bridge |

## Changing the protocol

- Additive (new request type, new optional field): no version bump. Old SDKs answer `unsupported_type`; old bundles ignore new fields.
- Breaking: bump `VERSION` on the side that changes, and keep serving the old version until the remote `minimumWebProtocol` says otherwise.
- Update this file, both implementations, and [`bridge_transcript.json`](../contracts/fixtures/bridge_transcript.json) in the same PR (the PR template asks for this). The transcript is what fails if you forget one side.
