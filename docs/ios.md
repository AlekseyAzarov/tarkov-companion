# iOS (headless)

For iOS game and app teams. The Swift package [`sdk-ios`](../sdk-ios) runs
the same C++ core as Android and offers its headless API: search and verdicts,
with the offline cache and the catalog fallback. The overlay, sign-in and
favourites are Android-only for now ([ADR 0013](adr/0013-ios-headless-layer.md)).

Requires iOS 15 (macOS 12 for tests).

## 1. Build the core and add the package

The core ships inside the package as an XCFramework, built from `core/`. On a
Mac with Xcode, CMake and Node.js:

```bash
scripts/build-ios-core.sh                # sdk-ios/Frameworks/TarkovCompanionCore.xcframework
node scripts/generate-swift-contract.mjs # Swift constants from contracts/
```

Then add `sdk-ios` as a local package (File → Add Package Dependencies → Add
Local) and link the `TarkovCompanion` library.

## 2. Initialize

```swift
import TarkovCompanion

var configuration = CompanionConfiguration()
configuration.catalogUrl = "https://alekseyazarov.github.io/tarkov-companion/catalog"
configuration.logger = { level, message in print("Companion [\(level)] \(message)") }
configuration.analytics = { event, properties in Analytics.log(event, properties) }
TarkovCompanion.initialize(configuration)
```

`initialize` does no I/O and doesn't start the core: that happens on the
first lookup. Call it once; later calls are ignored.

## 3. Tell it about the player

```swift
try TarkovCompanion.setPlayerProfile(PlayerProfile(level: 22, intelligenceCenterLevel: 1))
```

`PlayerProfile` throws `CompanionError` with `.invalidArgument` for a value
out of range. A profile set before `initialize` is kept until then.

## 4. Search and verdicts

```swift
let items = try await TarkovCompanion.searchItems("salewa")
let verdict = try await TarkovCompanion.recommendation(forItemId: items[0].id)

switch verdict.verdict {
case .keep: showKeep(verdict.keepReasons)
case .sellFlea: showFlea(verdict.flea)
case .sellTrader: showTrader(verdict.bestTrader)
case .noBuyer: showNoBuyer()
}
```

Errors are `CompanionError`; `code` says why (`.network`, `.api`,
`.schemaMismatch`, `.notFound`, `.invalidArgument`, ...). New codes may be
added in minor versions: keep a `default` case when you switch over them.
`fromOfflineCache` on a recommendation says it came from the cache because
the network failed.

Lookups run off the main thread; await them from wherever you like.
Cancelling the calling task cancels the request and throws
`CancellationError`. To replace a search as the player types, cancel the
previous task:

```swift
searchTask?.cancel()
searchTask = Task { try await TarkovCompanion.searchItems(query) }
```

## Bringing your own HTTP stack

```swift
struct GameTransport: HttpTransport {
    func execute(_ request: HttpRequest) async throws -> HttpResponse {
        let (body, statusCode, headers) = try await GameNetwork.send(
            request.method, request.url, request.headers, request.body,
            timeout: Double(request.timeoutMilliseconds) / 1000
        )
        return HttpResponse(statusCode: statusCode, headers: headers, body: body)
    }
}

configuration.httpTransport = GameTransport()
```

Return non-2xx responses rather than throwing; throw only when there is no
response at all. Without one, the SDK uses `URLSession` with no HTTP cache and
no redirects.

## Where it keeps data

- `Caches/tarkov-companion/cache-v1`: responses for offline use, at most 64
  entries; the system may clear it.
- `Application Support/tarkov-companion/data`: what the core keeps until it
  deletes it itself, never evicted.

File names are hashes of the keys, so they don't reveal search queries.
