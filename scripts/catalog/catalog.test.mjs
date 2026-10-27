import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { describe, it } from 'node:test';
import {
  buildGameModeFiles,
  everyQueryWordStartsAWordOfTheItem,
  searchWords,
  shardFileForQueryOrNull,
  shardFilesForItem,
} from './catalog.mjs';

const fixture = (name) =>
  JSON.parse(readFileSync(new URL(`../../contracts/fixtures/${name}`, import.meta.url), 'utf8'));

describe('search rules shared with core/src/catalog.cpp', () => {
  const cases = fixture('catalog_search.json');

  it('splits names into the same words', () => {
    for (const { text, words } of cases.words) {
      assert.deepEqual(searchWords(text), words, text);
    }
  });

  it('picks the same shard for a query', () => {
    for (const { query, shard } of cases.shards) {
      assert.equal(shardFileForQueryOrNull(query), shard, query);
    }
  });

  it('matches the same items', () => {
    for (const { query, name, shortName, matches: expected } of cases.matches) {
      const matched = everyQueryWordStartsAWordOfTheItem(searchWords(query), name, shortName);
      assert.equal(matched, expected, `${query} / ${name}`);
    }
  });

  it('files an item under every shard a matching query can pick', () => {
    const shardFiles = shardFilesForItem('Graphics card', 'GPU');

    for (const query of ['gr', 'graphics', 'card', 'gpu', 'GRAPHICS CARD']) {
      assert.ok(shardFiles.has(shardFileForQueryOrNull(query)), query);
    }
    assert.ok(!shardFiles.has(shardFileForQueryOrNull('salewa')));
  });
});

// A tiny game mode in json.tarkov.dev's shapes: two items, a trader, a task, a hideout station.
function tinyGameModeDatasets() {
  const items = {
    gpu: {
      id: 'gpu',
      name: 'gpu Name',
      shortName: 'gpu ShortName',
      iconLink: 'https://assets/gpu.webp',
      basePrice: 198000,
      avg24hPrice: 309606,
      lastLowPrice: 299000,
      width: 2,
      height: 1,
      types: ['barter'],
      sellToTrader: [
        { trader: 't1', priceRUB: 100980 },
        { trader: 'gone', priceRUB: 5 },
      ],
    },
    case: {
      id: 'case',
      name: 'case Name',
      shortName: 'case ShortName',
      basePrice: 1,
      avg24hPrice: null,
      lastLowPrice: 50,
      width: 1,
      height: 1,
      types: ['noFlea'],
      sellToTrader: [],
    },
    junk: { id: 'junk', name: 'junk Name', shortName: 'junk ShortName', types: [] },
  };

  return {
    items: { data: { items, fleaMarket: { name: 'FleaMarket' } } },
    tasks: {
      data: {
        tasks: {
          k1: { id: 'k1', name: 'k1 name', minPlayerLevel: 10, objectives: [{ type: 'giveItem', items: ['gpu'] }] },
          k2: { id: 'k2', name: 'k2 name', minPlayerLevel: 1, objectives: [{ type: 'sellItem', items: ['gpu', 'case'] }] },
        },
      },
    },
    traders: { data: { t1: { id: 't1', name: 't1 Nickname', normalizedName: 'therapist' } } },
    hideout: {
      data: { s1: { name: 'area_1_name', levels: [{ level: 2, itemRequirements: [{ item: 'gpu', count: 1 }] }] } },
    },
    items_en: {
      data: {
        'gpu Name': 'Graphics card',
        'gpu ShortName': 'GPU',
        'case Name': 'Card case',
        'case ShortName': 'Kappa',
        FleaMarket: 'Flea Market',
      },
    },
    items_ru: { data: { 'gpu Name': 'Видеокарта', 'gpu ShortName': 'Видеокарта', FleaMarket: 'Барахолка' } },
    tasks_en: { data: { 'k1 name': 'Profit Retention' } },
    traders_en: { data: { 't1 Nickname': 'Therapist' } },
    traders_ru: { data: { 't1 Nickname': 'Терапевт' } },
    hideout_en: { data: { area_1_name: 'Bitcoin farm' } },
  };
}

describe('buildGameModeFiles', () => {
  const files = buildGameModeFiles('regular', tinyGameModeDatasets(), {
    updated: '2026-10-28T00:00:00.000Z',
    minimumItemCount: 1,
  });
  const itemFile = (language, itemId) => files.get(`regular/${language}/items/${itemId}.json`)?.data.items[0];
  const shardItemIds = (language, query) =>
    files.get(`regular/${language}/search/${shardFileForQueryOrNull(query)}`)?.data.items.map((item) => item.id);
  const sortedKeys = (object) => Object.keys(object).sort();

  it('writes items in the shape of the GraphQL ItemById response', () => {
    // The same fields, nested the same way, as a recorded tarkov.dev response.
    const recorded = fixture('item_search.json').data.items[0];
    const graphicsCard = itemFile('en', 'gpu');

    assert.deepEqual(sortedKeys(graphicsCard), sortedKeys(recorded));
    assert.deepEqual(sortedKeys(graphicsCard.sellFor[0]), sortedKeys(recorded.sellFor[0]));
    assert.deepEqual(sortedKeys(graphicsCard.sellFor[0].vendor), sortedKeys(recorded.sellFor[0].vendor));
    assert.deepEqual(sortedKeys(graphicsCard.usedInTasks[0]), sortedKeys(recorded.usedInTasks[0]));
  });

  it('turns trader offers, the flea price and tasks into what the verdict needs', () => {
    const graphicsCard = itemFile('en', 'gpu');

    assert.equal(graphicsCard.name, 'Graphics card');
    assert.deepEqual(graphicsCard.sellFor, [
      { priceRUB: 100980, vendor: { name: 'Therapist', normalizedName: 'therapist' } },
      { priceRUB: 299000, vendor: { name: 'Flea Market', normalizedName: 'flea-market' } },
    ]);
    assert.deepEqual(graphicsCard.usedInTasks, [{ id: 'k1', name: 'Profit Retention', minPlayerLevel: 10 }]);
    assert.deepEqual(itemFile('en', 'case').sellFor, [], 'flea-banned: no flea offer');
  });

  it('translates, falling back to English', () => {
    const graphicsCard = itemFile('ru', 'gpu');

    assert.equal(graphicsCard.name, 'Видеокарта');
    assert.equal(graphicsCard.sellFor[0].vendor.name, 'Терапевт');
    assert.equal(graphicsCard.sellFor[1].vendor.name, 'Барахолка');
    assert.equal(graphicsCard.usedInTasks[0].name, 'Profit Retention');
    assert.equal(itemFile('ru', 'case').name, 'Card case');
  });

  it('skips items without a name', () => {
    assert.equal(itemFile('en', 'junk'), undefined);
  });

  it('files each item under the shards of its words, shortest names first', () => {
    assert.deepEqual(shardItemIds('en', 'graphics'), ['gpu']);
    assert.deepEqual(shardItemIds('en', 'kappa'), ['case']);
    assert.deepEqual(shardItemIds('ru', 'видео'), ['gpu']);
    // Both have a word starting with "car": "Card case" (9 letters) comes before "Graphics card" (13).
    assert.deepEqual(shardItemIds('en', 'card'), ['case', 'gpu']);
  });

  it('writes the hideout in the shape of the GraphQL Hideout response', () => {
    const hideoutStations = files.get('regular/en/hideout.json').data.hideoutStations;
    const recordedStation = fixture('hideout.json').data.hideoutStations[0];

    assert.deepEqual(hideoutStations, [
      { name: 'Bitcoin farm', levels: [{ level: 2, itemRequirements: [{ count: 1, item: { id: 'gpu' } }] }] },
    ]);
    assert.deepEqual(sortedKeys(hideoutStations[0]), sortedKeys(recordedStation));
  });

  it('refuses a dataset that is too small to be real', () => {
    assert.throws(
      () => buildGameModeFiles('regular', tinyGameModeDatasets(), { minimumItemCount: 1000 }),
      /fewer than 1000/,
    );
  });
});
