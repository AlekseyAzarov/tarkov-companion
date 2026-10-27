# ADR 0010: Favourites, local first, synced through the core

**Status:** accepted · 2026-10

## Context

Favourites are a short list of items a player checks often, kept per
player ([ADR 0009](0009-sign-in.md)). They have to work without sign-in, follow the
player across devices when signed in, and survive being offline, which in a
game is the normal case.

## Decision

The whole feature is in the core (`core/src/favorites.cpp`): storage format,
merge rules, the sync request, and what the page and the host see. The
platform stores bytes and nothing more. Integration details are in
[favorites.md](../favorites.md).

### Local first

Every change is applied and saved locally first, then synced. The list is
never "loading from the server": a player in a raid with no network sees and
edits it the same way. Changes made offline wait in an outbox saved with the
list, and go with the next sync: after the next change, at the next start,
or when the overlay opens or the host reads the list while they wait.

Signed out, the list is the device's. On sign-in it moves into the
account's list (per item, the later change wins) and is synced. On sign-out
the account's copy is deleted from the device, which starts again with an
empty list. Keeping the account's list after sign-out would hand one
player's favourites to whoever uses the device next; the server still has
them for the next sign-in.

### Last writer wins per item, by the player's clock

Each change carries `changedAt`, the device time when the player made it.
Per item, the later change wins. Removals stay as records
(`favorite: false`), so an offline device learns about them instead of
bringing the item back.

- **Why not a server timestamp:** an offline change would be stamped when it
  reached the server, and then win over a change made later on another
  device that synced first.
- **Why not a CRDT or per-field merge:** each item has one bit of state.
  Per-item LWW is what the data needs.
- **The risk is a wrong clock.** The device never stamps a change earlier
  than the previous one for the same item, and the server ignores changes
  more than 5 minutes in the future. Changes within the clock difference
  between two devices can still resolve the wrong way, and that is accepted
  for favourites.

### The server owns the cursor

The client sends the cursor from its last sync; the server answers with
every record changed since, plus its record for every item the client sent.
A refused change (over the limit, from the future) therefore comes back as
the server's version, and the client converges without special cases. The
cursor is the server's own counter, so client clocks play no part in what
is fetched.

### A sync URL of its own

`favoritesSyncUrl` is separate from the sign-in issuer. An identity server
and a game backend are usually different services, often run by different
teams. The request goes through the same authorized-request path as
everything else in the core: refresh before expiry, one retry after a 401.

### Names come from the catalog, never from the page

The page and the host pass an item id; the core takes the name, short name
and icon from items it has already received from tarkov.dev in this
session. An unknown id is `NOT_FOUND`. Neither a compromised page nor a
caller can put text of its own into the list, and through sync into other
devices. A list that only stored ids would avoid this too, but would need a
tarkov.dev request on every device before showing anything.

### A third store: data

The actions `data_read`, `data_write` and `data_delete` run against a
directory in `filesDir` that is never trimmed. The cache can be evicted at
any time, so it can't hold what the player made. Secrets are encrypted per
entry, which this doesn't need. Three kinds of storage, three action
families: the platform can't put one in the other by mistake.

### Sync on use, nothing in the background

No WorkManager, no periodic job. The list syncs when it is loaded, after
each change, again if changes queued up during a sync, and when the overlay
opens or the host reads the list while changes wait. An SDK inside a game
doesn't wake the device.

## Consequences

- **Public API:** `CompanionConfig.Builder.favoritesSyncUrl`,
  `TarkovCompanion.getFavorites` / `setFavorite` /
  `addFavoritesListener` / `removeFavoritesListener`, `FavoriteItem`,
  `FavoritesListener`. C API: `tarkov_companion_session_favorites_list`,
  `tarkov_companion_session_favorite_set`. Bridge: `favorites_list`, `favorite_set`,
  event `favorites_changed`, feature `favorites`.
- **Requests are registered before they start.** An answer can come back
  synchronously (an empty list, a verdict from a warm cache), so the bridge
  registers a request as in flight first and starts it second.
- **Known gaps:**
  - signing out while offline drops the queued changes along with the
    account's copy;
  - removals are kept forever, bounded by the number of items in the game.
- **Cost:** +30 KB on the arm64 library, 17.6 KB of it favourites code, the
  rest continuation thunks and unwind tables. +1 KB of dex.
