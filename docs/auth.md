# Sign-in

Players sign in with **your** identity server, so the companion can keep
things per player ([favourites](favorites.md)). The SDK speaks plain OAuth 2.0:
authorization code with PKCE in the system browser (RFC 6749, 7636, 8252).
Design and trade-offs: [ADR 0009](adr/0009-sign-in.md).

## Integrating

```kotlin
TarkovCompanion.initialize(this, CompanionConfig.Builder()
    .signIn(SignInConfig.Builder().issuer("https://id.example.com").clientId("game-123").build())
    .build())

// from a button; main thread
TarkovCompanion.signIn(activity, object : CompanionCallback<Account> {
    override fun onSuccess(result: Account) { /* result.displayName */ }
    override fun onError(error: CompanionException) {
        // SIGN_IN_CANCELLED: the player closed the page. SIGN_IN_FAILED, NETWORK: show a retry.
        // INVALID_ARGUMENT: sign-in isn't configured or can't work in this app; the message says why.
    }
})

TarkovCompanion.currentAccount                        // null when signed out
TarkovCompanion.addAccountListener { account -> }     // main thread, on every change
TarkovCompanion.signOut(callback)                     // immediate, works offline
```

The overlay shows a "Sign in" button on its own when sign-in is configured.
The result reaches the page the same way whoever started it: from the
game's own button or from the overlay.

After a cold start the SDK restores the sign-in from its secret store
shortly after `initialize()`. The account listener fires when it does.

## What your server needs

| Endpoint | Used for |
|---|---|
| `GET {issuer}/authorize` | the sign-in page; `response_type=code`, `code_challenge_method=S256` |
| `POST {issuer}/token` | `authorization_code` and `refresh_token` grants, form-encoded |
| `POST {issuer}/revoke` | sign-out (RFC 7009), best effort |
| `GET {issuer}/me` | `{"id": "...", "displayName": "..."}` for the Bearer token |

- **Redirect URI:** register `<applicationId>://tarkov-companion-auth` for the client,
  e.g. `com.example.game://tarkov-companion-auth`. The SDK's manifest declares that
  scheme with the host's `${applicationId}`, so the game configures nothing,
  as long as the applicationId can be a URI scheme (see below).
- **Refresh tokens:** rotate them, and revoke the whole family when a
  rotated-out token comes back (RFC 9700). The SDK handles both: the new
  refresh token replaces the old one, and `invalid_grant` signs the player out.
- **Rotation and lost responses:** if a refresh response is lost after the
  server rotated the token (a network drop at the wrong moment), the next
  refresh looks like a replay and the player is signed out. Production
  servers usually accept the previous refresh token for a few seconds to
  absorb this.

### An applicationId that can't be a scheme

A URI scheme is lowercase letters, digits, `+`, `-` and `.`. Android matches
intent-filter schemes case-sensitively, and browsers lowercase the scheme they
redirect to, so with an applicationId like `com.Studio.Game` or
`com.studio.my_game` the browser's answer would never reach the game. The SDK
checks this at `initialize()`: it logs an error, leaves sign-in off (the
overlay shows no "Sign in" button), and `signIn()` answers `INVALID_ARGUMENT`
saying why. Choose a scheme of your own and declare it for the SDK's redirect
activity; the manifest merger adds it to the SDK's declaration:

```kotlin
SignInConfig.Builder().issuer("https://id.example.com").clientId("game-123")
    .redirectScheme("com.studio.mygame")
    .build()
```

```xml
<activity android:name="com.tarkovcompanion.sdk.ui.RedirectActivity">
    <intent-filter>
        <action android:name="android.intent.action.VIEW" />
        <category android:name="android.intent.category.DEFAULT" />
        <category android:name="android.intent.category.BROWSABLE" />
        <data android:scheme="com.studio.mygame" android:host="tarkov-companion-auth" />
    </intent-filter>
</activity>
```

Register `com.studio.mygame://tarkov-companion-auth` on the server.

## What the SDK guarantees

- **System browser, never a WebView.** In-app pages could read the password,
  and Google refuses sign-ins from embedded WebViews. The page opens as a
  Custom Tab where the browser supports it, without the `androidx.browser`
  dependency.
- **PKCE and state:** a code intercepted on the way back is useless without
  the verifier, which never leaves the process. A redirect is redeemed only if
  it goes to exactly the registered URI (the scheme compared without regard to
  case) and carries the `state` of the sign-in in progress. Any other link is
  ignored and logged, and the real sign-in goes on: the redirect activity is
  exported, so any app can send one, and it must not be able to cancel a
  sign-in either.
- **Signing out wins:** a sign-out cancels a sign-in in progress, even one
  whose code is already being redeemed or whose account is being fetched;
  tokens issued meanwhile are dropped and revoked. A newer sign-in replaces
  an older one the same way.
- **Tokens at rest:** encrypted with AES-GCM under an Android Keystore key
  that can't be exported, in `noBackupFilesDir`. A file that can't be
  decrypted (the key was invalidated, the file was tampered with or copied)
  reads as "signed out", never as a crash.
- **Tokens stay in the core:** the web page only learns "signed in as
  &lt;name&gt;", and tokens are never written to logs or analytics. Both are
  covered by tests in `core/tests/authentication_test.cpp`.
- **Refresh:** done 30 seconds before the access token expires, once for all
  the requests waiting on it. A 401 gets one refresh and one retry. Only an
  answer that says the refresh token is no good signs the player out:
  `400 invalid_grant` or `401`. With no network, a rate limit (`429`), any
  other `4xx` or a server error, the player stays signed in and the next
  request tries the refresh again, so an incident on the identity server
  doesn't sign everyone out.
- **Token lifetime:** an `expires_in` that is negative reads as "already
  expired" (refreshed before first use); one over a year is capped, so a
  broken server can't overflow the clock arithmetic.

## Development server

`bff/` holds a minimal authorization server for trying this end to end. It
is not for production: it keeps everything in memory, and any name signs
you in.

```bash
node bff/src/main.mjs            # http://127.0.0.1:8787
./gradlew :host-game:installWithSdkDebug
```

The sample game's debug build signs in against `http://10.0.2.2:8787`, which
is the computer as the emulator sees it. Plain http is accepted only because:

- the core allows http only in `developmentMode`, which the SDK sets just for
  debuggable apps, and only to `127.0.0.1`, `localhost` or `10.0.2.2`;
- the debug build's [network security config](../host-game/src/withSdkDebug/res/xml/demo_network_security.xml)
  allows cleartext to `10.0.2.2` and nothing else.

## Troubleshooting

| Symptom | Cause |
|---|---|
| `SIGN_IN_FAILED` "identity server: invalid_request" | the server rejected the authorize request: wrong client id, or the redirect URI isn't registered |
| `SIGN_IN_CANCELLED` right away | no browser is installed (logged as "no browser to sign in with"), or the player backed out |
| `INVALID_ARGUMENT` "sign-in is off: the applicationId ... can't be a URI scheme" | set `redirectScheme()` and declare it, as above |
| the browser shows the server's page, then nothing happens | the redirect URI's scheme isn't declared for `RedirectActivity`, or isn't lowercase |
| signed out after a while | the refresh token expired or was revoked; see "Rotation and lost responses" above |
| `NETWORK` on sign-in with a dev server | the host's network security config blocks http: allow the dev host in debug builds only |

Every failed sign-in is logged by the SDK with its reason, never with a token or a code.
