# Remote flags and kill switch

Optional. Point the SDK at a JSON document you control:

```kotlin
CompanionConfig.Builder().remoteFlagsUrl("https://config.example.com/companion/flags.json")
```

Example: [`config/flags.json`](../config/flags.json).

```json
{
  "schema": 1,
  "webFlowEnabled": true,
  "disabledSdkVersions": ["0.1.0"],
  "disabledBundleVersions": [],
  "minimumWebProtocol": 1
}
```

| Field | Effect |
|---|---|
| `webFlowEnabled` | `false`: `showItemLookup` returns `DISABLED_REMOTELY` for every SDK version |
| `disabledSdkVersions` | same, for listed SDK versions only (a bad release in the wild) |
| `disabledBundleVersions` | the handshake refuses these web bundles (`bundle_disabled`) |
| `minimumWebProtocol` | the handshake refuses bundles speaking an older protocol |

## Behaviour

Implemented in the C++ core ([`remote_flags.cpp`](../core/src/remote_flags.cpp), the flow in [`session.cpp`](../core/src/session.cpp)), so every platform behaves the same ([ADR 0008](adr/0008-remote-flags-and-analytics-catalog.md)).

- Loaded once per process, through the host's `HttpTransport`. With a flags URL (or sign-in) configured, the SDK opens its native core as soon as the main thread first goes idle after `initialize()`, on a background thread; without either, the core loads only on the first lookup.
- The last good document is persisted. A kill switch survives a cold start **without network**: the cached document is applied before the network refresh.
- On first ever launch, defaults apply until the first fetch completes. A kill switch therefore takes effect within one session, not instantly. That's the trade-off for keeping `initialize()` free of I/O.
- Documents with an unknown `schema`, or that don't parse, are ignored and logged. A bad push cannot brick the SDK.
- Unknown fields are ignored, and fields of the wrong type keep their defaults, so new flags can be added without breaking old SDKs.
- The web bundle checks (`minimumWebProtocol`, `disabledBundleVersions`) run at handshake time, against the flags in force then: a page opened before the document arrived is still subject to it.

## Trying it

The sample game's debug build serves its flags from a file on the device. Turn the kill switch on, restart the game twice (once to fetch and persist, once to apply on a cold start), and the companion button shows "temporarily unavailable":

```bash
echo '{"schema":1,"webFlowEnabled":false}' > demo-flags.json
adb push demo-flags.json /data/local/tmp/
adb shell run-as com.tarkovcompanion.hostgame cp /data/local/tmp/demo-flags.json files/
```

Delete `files/demo-flags.json` to turn it off again.

## Runbook: a release is breaking games

1. Add the version to `disabledSdkVersions`. Hosts see `DISABLED_REMOTELY` and should hide their entry point.
2. Watch `companion_launch_blocked` climb and `bridge_request_failed` drop.
3. Ship the fix, then remove the entry.
