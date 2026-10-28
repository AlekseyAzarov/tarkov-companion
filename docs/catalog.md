# Catalog

tarkov.dev's GraphQL API, where the SDK gets prices, names, tasks and the
hideout, has been down since July 2026
([the-hideout/tarkov-api#474](https://github.com/the-hideout/tarkov-api/issues/474)).
Its JSON datasets on `json.tarkov.dev` still work, but they come only as whole
files: 2.3 MB compressed for the items alone, 17 MB once unpacked, far too
much for a phone.

The catalog is those datasets cut into small static files ahead of time, in
the shapes of the GraphQL responses the SDK already reads. A scheduled job
rebuilds it every hour and publishes it on GitHub Pages: no server to run.
Design and trade-offs: [ADR 0011](adr/0011-static-catalog.md).

## Integrating

```kotlin
TarkovCompanion.initialize(this, CompanionConfig.Builder()
    .catalogUrl("https://<owner>.github.io/<repository>/catalog")
    .build())
```

Without it, an outage leaves only the offline cache. With it, a read that
fails on the GraphQL API goes to the catalog; after such a failure, reads go
straight to the catalog for 10 minutes. If the catalog fails too, the offline
cache is served. The logs say which source
answered: `tarkov.dev's GraphQL API failed; reading the catalog for the next
10 minutes`.

## What the device downloads

| Read | File | Size (gzip, October 2026) |
|---|---|---:|
| Search | the shard of the query's longest word, by its first 3 letters (2 for a 2-letter word) | median 0.6 KB, 90% under 2.4 KB; worst 36 KB (3 letters), 54 KB (2 letters) |
| Verdict for an item not yet seen | the item's file | ~0.4 KB |
| Hideout | one file, kept for a day | 2.5 KB |

The large shards are common beginnings (`ar`, `ma`) and calibers (`62` from
`7.62x39`). They hold whole items, trader offers and tasks included, because
a verdict for an item found in search uses what the search returned.

## Layout

```
catalog/manifest.json                                 when it was built, from what
catalog/<regular|pve>/<en|ru>/items/<item id>.json     ItemById response
catalog/<regular|pve>/<en|ru>/search/<shard>.json      ItemSearch response
catalog/<regular|pve>/<en|ru>/hideout.json             Hideout response
```

Other languages read English. A shard's name is its letters' UTF-8 bytes in
hex: `677261.json` is "gra".

## How search works without a search engine

A shard holds every item with a word that starts with the shard's letters.
The SDK picks the shard of the query's longest word and keeps the items in
which every query word starts some word of the name or the short name, at
most 20, shortest names first. "aid kit" finds the Salewa first aid kit;
"raphics" doesn't find the graphics card, which tarkov.dev's substring search
would.

Words are runs of ASCII letters and digits, accented Latin letters and
Cyrillic, lowercased, with ё read as е. The generator
([`scripts/catalog/catalog.mjs`](../scripts/catalog/catalog.mjs)) and the SDK
([`core/src/catalog.cpp`](../core/src/catalog.cpp)) apply the same rules and
are tested against the same cases,
[`catalog_search.json`](../contracts/fixtures/catalog_search.json).

## How the data maps

| GraphQL field | From the datasets |
|---|---|
| `name`, `shortName` | `items_<lang>`, English when a Russian name is missing |
| `sellFor` (traders) | `sellToTrader.priceRUB`; trader names from `traders_<lang>` |
| `sellFor` (flea) | `lastLowPrice`, unless the item is flea-banned (`noFlea`): what the GraphQL API reported too |
| `usedInTasks` | tasks with a `giveItem`, `findItem` or `plantItem` objective listing the item |
| hideout | `itemRequirements` of each station level |

Known gaps: the tasks dataset lacks 11 of the 510 tasks the GraphQL API had,
so those give no "keep" reason; prices are up to an hour old.

## Building it

```bash
node scripts/build-catalog.mjs                 # build/catalog, about 30 s
node scripts/build-catalog.mjs --max-age 60    # only if the one there is older than an hour
node scripts/build-catalog.mjs --from <dir>    # from saved <mode>_<dataset>.json files
```

The development server serves `build/catalog` at `/catalog/`, and the sample
game's debug build uses it (`http://10.0.2.2:8787/catalog`). A dataset with
fewer than 1000 items fails the build instead of publishing a broken catalog.

## Publishing it

[`.github/workflows/catalog.yml`](../.github/workflows/catalog.yml) rebuilds
it at :41 every hour and deploys it to GitHub Pages under `/catalog/`. It
needs the repository on GitHub with Pages enabled and its source set to
"GitHub Actions". GitHub may delay scheduled runs and disable them after 60 days without
activity in a public repository; a failed run publishes nothing, so the last
good catalog stays up.

The data is tarkov.dev's, published by [the-hideout](https://github.com/the-hideout);
the catalog only reshapes it.
