# ADR 0009: Sign-in with OAuth and PKCE, the flow in the core

**Status:** accepted · 2026-10

## Context

The companion keeps per-player state (favourites synced across devices),
which needs a signed-in player. Most games already have an identity server;
the SDK speaks the standard to it rather than bringing its own.

## Decision

OAuth 2.0 authorization code with PKCE, in the system browser. Integration
details are in [auth.md](../auth.md).

### The browser, not the SDK's WebView

RFC 8252 and Google's policy both rule out signing in inside an embedded
WebView: the app could read the password, and the player can't see whose
page it is. The flow is:

1. The page opens as a Custom Tab.
2. The browser returns to `<applicationId>://tarkov-companion-auth`.
3. `RedirectActivity` hands that URI to the `SignInActivity` waiting for it.
   This is the pattern AppAuth uses.

The Custom Tab needs no `androidx.browser` dependency: it is a plain
`ACTION_VIEW` intent with the session extra (ADR 0003).

The redirect scheme is `${applicationId}` in the SDK's own manifest, so a
host needs no placeholder or manifest entry. A scheme must be lowercase
letters, digits, `+`, `-` and `.`, and Android matches it case-sensitively
while browsers lowercase it, so an applicationId with uppercase letters or
`_` can't be one: such a host sets `SignInConfig.Builder.redirectScheme()`
and declares that scheme for `RedirectActivity`. Without it, sign-in stays
off with a logged reason rather than starting a sign-in that never returns.

### The flow is in the core; randomness and storage are the platform's

**In the core (`core/src/authentication.cpp`):**

- PKCE and `state`;
- the redirect check and the code exchange;
- refresh: one for every waiting request, 30 s before expiry, once more
  after a 401;
- rotation, and forced sign-out only on `invalid_grant` or a 401 (a rate
  limit or an outage of the identity server keeps the session);
- sign-out with best-effort revocation;
- restore at start.

A second platform reuses it as is.

**From the platform:**

- **Randomness.** The core has no entropy source. The platform passes 48
  fresh `SecureRandom` bytes with each sign-in, so the core stays
  deterministic and testable against RFC 7636's worked example.
- **Storage.** The actions `secret_read`, `secret_write` and `secret_delete`
  go to a store separate from the cache. On Android it is AES-GCM under a
  non-exportable Keystore key, in `noBackupFilesDir`, with the entry name as
  associated data. Separate actions mean a token can't end up in the
  plaintext cache by accident.

### SHA-256 written in the core

PKCE needs SHA-256, and the core links no crypto library. The hash is about
120 lines, checked against NIST vectors and the block-boundary lengths where
padding bugs hide. It only hashes; nothing is encrypted with code of our own.

### What the page and the logs may see

The page gets `{"signedIn": true, "displayName": "..."}` and nothing else,
through `hello` and the `account_changed` event. It can ask for `sign_in` and
`sign_out`. Tokens never reach it, the logs, or analytics. A test drives the
noisiest path (refresh, retry, forced sign-out) and searches every log line,
analytics value and page message for the issued tokens.

### http for development only

`developmentMode`, set only for debuggable host builds, lets the core accept
http URLs to loopback and `10.0.2.2` (the computer, seen from the emulator).
Anything else stays https-only. Without it, trying the flow locally would
need a TLS setup on every developer machine.

### A development server in the repository

`bff/` is a minimal authorization server: in-memory, any name signs in, Node
built-ins only, with tests. It is labelled as not for production. The client
side works with any compliant server.

## Consequences

- **Public API:** `SignInConfig`, `Account`, `AccountListener`,
  `TarkovCompanion.signIn` / `signOut` / `currentAccount` / listeners, and
  three `ErrorCode` entries.
- **A known gap: process death during sign-in.** The PKCE verifier lives in
  the core's memory, so a redirect into a fresh process is ignored (and
  logged), and the player taps "Sign in" again. Persisting the verifier
  would close this at the cost of a secret on disk for a few minutes.
- **Cost:** +48 KB on the arm64 library (26 KB of it auth and SHA-256, the
  rest continuation thunks and unwind tables), +5 KB of dex.
