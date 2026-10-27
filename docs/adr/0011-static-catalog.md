# ADR 0011: A static catalog when tarkov.dev's GraphQL API is down

**Status:** accepted · 2026-10

## Context

tarkov.dev's GraphQL API, the SDK's only data source, has answered every
request with "GraphQL server unavailable" since July 2026
([the-hideout/tarkov-api#474](https://github.com/the-hideout/tarkov-api/issues/474)).
The offline cache only helps players who looked an item up before the outage.

The same data is still published on `json.tarkov.dev`, which tarkov.dev's own
site reads. It comes only as whole datasets per game mode: items are 1.9 MB
gzip (17 MB unpacked), their names another 0.4 MB per language, tasks 0.4 MB.
There is no search and no single-item endpoint. Item names are translation
keys resolved by separate files.

## Options

| | A proxy server | Datasets on the device | A static catalog |
|---|---|---|---|
| Server to run | yes | no | no (GitHub Pages) |
| Download per refresh, per device | as with GraphQL | 2.3+ MB | as with GraphQL or less |
| Memory on the device | as now | 100+ MB to parse 17 MB with the core's DOM parser | as now |
| SDK changes | an http-for-development tweak | streaming parser, on-disk index | a fallback, a filter (+18 KB arm64) |
| Price freshness | minutes | minutes | up to an hour |

The proxy is the usual answer, and still the right one for a game with a
backend. The datasets on the device cost too much traffic and memory for a
library inside someone's game.

## Decision

A static catalog: a scheduled job cuts the datasets into small files and
publishes them on GitHub Pages. Details: [catalog.md](../catalog.md).

### Files in the shapes of the GraphQL responses

An item file is an `ItemById` response, a search shard an `ItemSearch`
response, the hideout file a `Hideout` response. The core's parser, engine
and offline cache stay as they are, and the generator's tests check the
shapes against the recorded GraphQL fixtures.

### Search shards by the first letters of words

The catalog can't search, so the device does, over a small candidate set: a
shard per 2- and 3-letter word beginning. The core picks the shard of the
query's longest word and keeps the items where every query word starts a
word. Measured on October 2026 data: median 0.6 KB gzip per search, worst
54 KB, against 2.3 MB for the datasets. Shards hold whole items because a
verdict for an item found in search uses what the search returned; slimmer
shards would need a second fetch per verdict.

The word rules (Latin, Cyrillic, ё as е, nothing else) are written twice, in
the generator and in the core, and both test against one file of cases
(`contracts/fixtures/catalog_search.json`), as the bridge transcript does for
the protocol.

### GraphQL first, then the catalog, then the cache

The GraphQL API stays the primary source: when it comes back, prices are
fresh again without a release. After it fails, reads skip it for 10 minutes,
so an outage doesn't cost each request a round trip. When the catalog fails
too, the GraphQL error is the one reported: it explains the outage.

### Building in CI, publishing on Pages

The build refuses datasets with fewer than 1000 items, and a failed run
publishes nothing, so a bad upstream push can't replace a good catalog with
an empty one.

## Consequences

- **Public API:** `CompanionConfig.Builder.catalogUrl`, config key `catalogUrl`.
- **Prices up to an hour old** in an outage, and the search matches word
  beginnings, not substrings.
- **Data gaps upstream:** 11 tasks missing from the dataset give no "keep".
- **Hosting:** needs the repository on GitHub with Pages; scheduled runs can
  be late and stop after 60 days without activity.
- **The debug build** serves recorded fixtures only with no network at all;
  an error from tarkov.dev passes through, so a developer sees the real
  fallback.
- **Cost:** +17.9 KB on the arm64 library (8.6 KB of it catalog code, the
  rest continuation thunks and unwind tables), +0.1 KB of dex.
