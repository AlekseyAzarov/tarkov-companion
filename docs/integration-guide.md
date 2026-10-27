# Integration guide

For game and app teams embedding the Tarkov Companion SDK.

## 1. Add the dependency

```kotlin
// settings.gradle.kts
dependencyResolutionManagement {
    repositories {
        maven("https://maven.pkg.github.com/AlekseyAzarov/tarkov-companion") {
            credentials { username = ...; password = ... } // GitHub Packages needs a token with read:packages
        }
    }
}

// app/build.gradle.kts
dependencies {
    implementation("com.tarkovcompanion:sdk-android:0.1.0")
}
```

What you get: one AAR, **no transitive dependencies**. The SDK adds the `INTERNET` permission and three activities to your manifest: the overlay and the sign-in holder (not exported), and the activity the browser returns to after a sign-in (exported, for the `<applicationId>://tarkov-companion-auth` link only).

**ABIs.** The AAR contains `arm64-v8a`, `armeabi-v7a` and `x86_64`. If your app sets `abiFilters`, the device's ABI must be in both lists, or lookups fail with `NATIVE_UNAVAILABLE` (see [troubleshooting](troubleshooting.md#native_unavailable--unsatisfiedlinkerror)).

## 2. Initialize

In `Application.onCreate`:

```kotlin
TarkovCompanion.initialize(
    this,
    CompanionConfig.Builder()
        .httpTransport(yourTransport)   // optional: route traffic through your HTTP stack
        .logger(yourLogger)             // optional: defaults to logcat
        .analytics(yourAnalytics)       // optional: events listed in docs/analytics.md
        .build(),
)
```

`initialize` does no disk, network or native-library work; it is safe on the cold-start path (traced as `TarkovCompanion.initialize` in Perfetto). Calling it twice is a no-op.

Initialize in `Application`, not lazily in an activity: Android can restore the companion screen directly after process death, before any of your activities run. See [troubleshooting](troubleshooting.md#overlay-closes-immediately-after-returning-to-the-game).

## 3. Tell it about the player

```kotlin
TarkovCompanion.setPlayerProfile(
    PlayerProfile.Builder()
        .level(player.level)               // flea market unlocks at 15
        .intelligenceCenterLevel(hideout.intel)   // level 3 cuts flea fees by 30%
        .gameMode(GameMode.PVE)
        .build()
)
```

Call it whenever these change; the next verdict uses the new values.

## 4. Open it

From the main thread, with any `Activity`:

```kotlin
when (TarkovCompanion.showItemLookup(activity, initialQuery = null)) {
    LaunchResult.LAUNCHED -> Unit
    LaunchResult.DISABLED_REMOTELY -> hideTheButton()   // kill switch, see remote-flags.md
    LaunchResult.NOT_INITIALIZED -> logBug()
}
```

The overlay is a translucent activity: your activity receives `onPause`/focus loss, exactly as for a system dialog. It follows your orientation (`screenOrientation="behind"`). If your engine pauses the game loop or audio in `onPause`, that is what the player sees while the overlay is open; keep rendering if you want the game visible and moving behind it (`onPause` is not `onStop`: your surface stays). Why an activity and not a view in your window: [ADR 0012](adr/0012-overlay-activity.md).

### Opening fast

The first open in a process starts the WebView engine: on the emulator the overlay is ready about 650 ms after the tap, against 250 ms for later opens. `TarkovCompanion.prewarm()` does that work ahead of time and brings the first open to about 450 ms. It costs a pause of the main thread (about 230 ms) when called, and about 45 MB of memory for the rest of the process, so call it when the player is likely to open the companion, as the menu with its button opens, not at game start. Numbers: [footprint.md](footprint.md#opening-the-overlay).

### From C++

The game never needs to link against the SDK. Ask your platform layer to open it, as the sample host does:

```cpp
// host-game/src/main/cpp/game_main.cpp
jclass cls = env->GetObjectClass(activity);              // not FindClass: see troubleshooting
jmethodID open = env->GetMethodID(cls, "openCompanionFromNative", "()V");
env->CallVoidMethod(activity, open);
```

```kotlin
// simplified from host-game/.../GameActivity.kt
fun openCompanionFromNative() = runOnUiThread { TarkovCompanion.showItemLookup(this) }
```

Keep that method in R8 (`-keep class ...GameActivity { void openCompanionFromNative(); }`): R8 cannot see JNI callers.

## 5. Headless use (your own UI)

```kotlin
val handle = TarkovCompanion.searchItems("salewa", object : CompanionCallback<List<ItemSummary>> {
    override fun onSuccess(result: List<ItemSummary>) { ... }
    override fun onError(error: CompanionException) { ... error.code ... }
})
// later, e.g. in onDestroy:
handle.cancel()   // guaranteed: no callback after this returns on the callback thread
```

Callbacks run on the main thread unless you set `callbackExecutor`.

Nothing in `TarkovCompanion` throws, whatever the order of calls: a call with a callback reports `NOT_INITIALIZED` (or `INVALID_ARGUMENT` for a configuration that can't work) through it, `showItemLookup` returns a `LaunchResult`, a profile or listeners given before `initialize()` take effect when it runs, and `prewarm()` before it does nothing. Only the configuration builders throw, for a value that is wrong where you wrote it.

The SDK runs its work on one background thread, in order, and HTTP on four threads of its own. A host `HttpTransport` that hangs only holds up other HTTP, never the overlay's messages, cache reads or cancellations; still, honour `timeoutMilliseconds`.

## Sign-in (optional)

Players can sign in with your identity server (OAuth 2.0 + PKCE in the
system browser): `CompanionConfig.Builder().signIn(...)`, then
`TarkovCompanion.signIn(activity, callback)`. Register
`<applicationId>://tarkov-companion-auth` as the redirect URI; an
applicationId with uppercase letters or `_` needs a scheme of its own. Full
guide: [auth.md](auth.md).

## Favourites

Work out of the box, on the device. With sign-in and
`CompanionConfig.Builder().favoritesSyncUrl(...)` they follow the player
across devices. API: `TarkovCompanion.getFavorites`, `setFavorite`,
`addFavoritesListener`. What your server needs: [favorites.md](favorites.md).

## When tarkov.dev is down

tarkov.dev's GraphQL API has been down since July 2026. Point the SDK at the
static catalog and lookups keep working, with prices up to an hour old:
`CompanionConfig.Builder().catalogUrl(...)`. How it is built and published:
[catalog.md](catalog.md).

## Bringing your own HTTP stack

```kotlin
class OkHttpTransport(private val client: OkHttpClient) : HttpTransport {
    override fun execute(request: HttpRequest): HttpResponse {
        // JSON for GraphQL, a form for the sign-in's token requests: take the type the SDK set.
        val body = request.body?.toRequestBody(request.headers["Content-Type"]?.toMediaTypeOrNull())
        val call = client.newCall(
            Request.Builder().url(request.url).method(request.method, body)
                .headers(request.headers.toHeaders()).build()
        )
        call.timeout().timeout(request.timeoutMilliseconds.toLong(), TimeUnit.MILLISECONDS)
        call.execute().use { r -> return HttpResponse(r.code, r.headers.toMap(), r.body.bytes()) }
    }
}
```

Return non-2xx responses rather than throwing; throw `IOException` only when there is no response at all.

Every byte the SDK sends goes through this, including item icons shown in the WebView (proxied via `/companion/icon`). So your proxy, certificate pinning and traffic accounting apply to the SDK too.

## Upgrading

The SDK follows semver. Public API changes are visible in [`sdk-android/api/sdk-android.api`](../sdk-android/api/sdk-android.api) and in [CHANGELOG.md](../CHANGELOG.md). Enums (`Verdict`, `ErrorCode`, `LaunchResult`) may gain values in minor versions: keep an `else` branch in `when`.
