# Favourites

Players star items in the overlay, or your game does it through the API.
Signed out, the list lives on the device. Signed in, it belongs to the
account and follows the player to every device where they sign in. Design
and trade-offs: [ADR 0010](adr/0010-favorites-sync.md).

## Integrating

Favourites work without any configuration, on the device only. To sync
them, add sign-in ([auth.md](auth.md)) and your sync endpoint:

```kotlin
TarkovCompanion.initialize(this, CompanionConfig.Builder()
    .signIn(SignInConfig.Builder().issuer("https://id.example.com").clientId("game-123").build())
    .favoritesSyncUrl("https://api.example.com/companion/favorites/sync")
    .build())

TarkovCompanion.getFavorites(callback)                 // List<FavoriteItem>, most recently added first
TarkovCompanion.setFavorite(itemId, true, callback)    // or false to remove
TarkovCompanion.addFavoritesListener { favorites -> }  // main thread, the whole list on every change
```

- **Names come from the SDK, not from you.** `setFavorite` takes an item id
  the player has searched for or looked at in this session, and takes the
  name and icon from there. An unknown id fails with `NOT_FOUND`. A page or a
  caller therefore can't put text into the list that tarkov.dev didn't
  provide.
- **Limit:** 200 favourites (`favoritesMaximum` in
  [config-limits.json](../contracts/config-limits.json)). Adding one more
  fails with `INVALID_ARGUMENT`.
- **The listener** fires after local changes, after a sync brings changes
  from another device, and on sign-in or sign-out, when the list changes
  owner.

The overlay shows the list when the search box is empty, and a star next to
the verdict. Both appear on their own.

## Signing in and out

- **Sign-in:** what the player starred on this device while signed out moves
  into the account and is synced. Per item, the later change wins.
- **Sign-out:** the account's copy is deleted from the device, and the
  device starts again with an empty list. The favourites are still on the
  server and come back with the next sign-in.

## When it syncs

After the list loads (at start, or after a sign-in), and after every change.
There is no background schedule: nothing wakes the device for favourites.
Changes made offline wait in a queue on disk and go with the next sync, which
is also tried whenever the overlay opens or the host reads the list while
changes are waiting, so they don't wait for the next change or start.
A failed sync is logged, at debug level for no network and as a warning for
an HTTP error, and the change stays queued.

## What your server needs

One endpoint, authorized with the access token from sign-in
(`Authorization: Bearer ...`). An expired token is refreshed before the
request; a 401 gets one refresh and one retry.

```
POST {favoritesSyncUrl}
{ "since": 41,
  "changes": [ { "itemId": "57347ca924597744596b4e71", "favorite": true, "changedAt": 1793108040000,
                 "name": "Graphics card", "shortName": "GPU", "iconLink": "https://..." } ] }

200
{ "cursor": 57,
  "changes": [ { "itemId": "...", "favorite": false, "changedAt": 1793107000000,
                 "name": "...", "shortName": "...", "iconLink": "..." } ] }
```

- **`changedAt`** is when the player made the change, in epoch milliseconds
  by the device's clock. Per item, the later change wins; on a tie, keep the
  server's record.
- **Removals are records** (`"favorite": false`), not deletions: a device
  that was offline has to learn about them.
- **`since` / `cursor`:** the server's own counter of accepted changes. Answer
  with every record changed after `since`, plus your record for each item the
  client sent, whether its change won or not.
- **Refusing a change:** answer with your record for that item, or
  `"favorite": false` if there is none. The client takes what you return.
  Reasonable refusals are: over the limit, and `changedAt` far in the future
  (a device with a wrong clock would otherwise win every conflict).
- **Names:** store what the client sends and return it. A record without
  names keeps the ones the device already has.

The development server in `bff/` implements all of this (see
[`server.mjs`](../bff/src/server.mjs), tests in
[`favorites.test.mjs`](../bff/test/favorites.test.mjs)). The sample game's
debug build uses it at `http://10.0.2.2:8787/favorites/sync`.

## Known limits

- **Clocks decide conflicts.** Two devices changing the same item within the
  clock difference between them can resolve the wrong way. For a list of
  favourites that is accepted; the server's 5-minute "future" bound limits
  how wrong a clock can be.
- **Signing out offline loses queued changes.** The account's copy, queue
  included, is deleted on sign-out.
- **Removals stay as records,** on the server and on the device. They are
  bounded by the number of items in the game (a few thousand).
