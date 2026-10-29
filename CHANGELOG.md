# Changelog

All notable changes to the SDK. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow [semver](https://semver.org/). Public API changes are also visible in `sdk-android/api/sdk-android.api`.

## [Unreleased]

### Added
- iOS: a Swift package (`sdk-ios`) with the headless API, search and verdicts with the offline cache and the catalog fallback, on the same core built as an XCFramework ([iOS guide](docs/ios.md), [ADR 0013](docs/adr/0013-ios-headless-layer.md)).

## [0.1.0] - 2026-10-28

### Added
- `TarkovCompanion` entry point: `initialize`, `prewarm`, `setPlayerProfile`, `showItemLookup`, headless `searchItems` / `getRecommendation`. Nothing in it throws: problems are reported through callbacks or `LaunchResult`, and a profile or listeners given before `initialize()` take effect when it runs.
- Verdicts: keep (quests, hideout) / sell on flea (after fee) / sell to trader / no buyer, with price per slot.
- Host interfaces: `HttpTransport`, `CompanionLogger`, `AnalyticsSink`.
- Offline cache with stale-price labelling; remote flags and kill switch; a static catalog as the fallback when tarkov.dev's GraphQL API is down ([catalog](docs/catalog.md)).
- Sign-in with the game's identity server: OAuth 2.0 with PKCE in the system browser, rotating refresh tokens kept in the core and encrypted under an Android Keystore key; `SignInConfig.Builder.redirectScheme()` for an applicationId that can't be a URI scheme ([auth](docs/auth.md)).
- Favourites, local first and synced across devices for signed-in players ([favourites](docs/favorites.md)).
- Bridge protocol v1 between the WebView flow and native ([protocol](docs/bridge-protocol.md)).
- C API, `TARKOV_COMPANION_ABI_VERSION 2`.
