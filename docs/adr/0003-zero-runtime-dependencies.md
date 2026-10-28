# ADR 0003: No runtime dependencies (no OkHttp, coroutines, AndroidX)

**Status:** accepted · 2026-10

## Context

Every dependency an SDK declares becomes the host's dependency. Games pin library versions for long stretches; an SDK that pulls OkHttp 5 into a game on OkHttp 4, or a newer `kotlinx-coroutines` than the game's other SDKs, creates conflicts the game team can only work around with exclusions.

## Decision

The SDK's runtime classpath is empty apart from the Kotlin stdlib.

| Need | Instead of | We use |
|---|---|---|
| HTTP | OkHttp / Retrofit | `HttpTransport` interface; host plugs in its own stack. Default: `HttpURLConnection` (OkHttp-backed on Android anyway) |
| JSON | Moshi / kotlinx.serialization | parsing in the C++ core; `org.json` (part of Android) for small envelopes |
| Async | coroutines | `Executor` + `Cancellable` callbacks; works from Java and through JNI |
| UI | Compose / AppCompat | one plain `Activity` + WebView |
| JS bridge | `androidx.webkit` | platform `WebMessagePort` (API 23+) |
| Logging, analytics | Timber, an analytics SDK | `CompanionLogger` / `AnalyticsSink` interfaces |

## Consequences

- No dependency conflicts with the host. Dex cost after R8: 40 KB.
- Even the stdlib is kept conservative: the SDK compiles with `languageVersion`/`apiVersion` 2.2 and depends on `kotlin-stdlib` 2.2.21, although the build itself uses Kotlin 2.4. A host on an older Kotlin toolchain can read our class metadata and isn't force-upgraded.
- The public API stays Java- and JNI-friendly: no `suspend` functions, no `Flow`.
- A little more code of our own: a small `HttpURLConnection` transport and a serial executor, both covered by tests.
- Hosts routing traffic through `HttpTransport` get their proxy, certificate pinning and metrics applied to the SDK, item icons included.
