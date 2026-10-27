// The static catalog's rules and builder (docs/catalog.md). Search words and
// shard choice must match core/src/catalog.cpp; both test contracts/fixtures/catalog_search.json.
import { readFileSync } from 'node:fs';

export const SOURCE = 'https://json.tarkov.dev';
export const GAME_MODES = ['regular', 'pve'];
// Also hard-coded in core/src/catalog.cpp (language()).
export const LANGUAGES = ['en', 'ru'];

const LIMITS = JSON.parse(readFileSync(new URL('../../contracts/config-limits.json', import.meta.url), 'utf8'));
const SHARD_LETTERS = LIMITS.shared.catalogShardLetters;
const SHARD_MINIMUM_LETTERS = LIMITS.shared.catalogShardMinimumLetters;
// Objectives that take the item away from the player: the verdict is "keep".
// sellItem is not one: its objectives list nearly every item.
const OBJECTIVES_TAKING_THE_ITEM = new Set(['giveItem', 'findItem', 'plantItem']);
const FLEA_VENDOR = 'flea-market';
// A dataset smaller than this is an upstream problem: publish nothing rather than a broken catalog.
const DEFAULT_MINIMUM_ITEM_COUNT = 1000;

// ---------------------------------------------------------------- search rules (= core/src/catalog.cpp)

const isWordCodePoint = (codePoint) =>
  (codePoint >= 0x30 && codePoint <= 0x39) ||
  (codePoint >= 0x61 && codePoint <= 0x7a) ||
  (codePoint >= 0xc0 && codePoint <= 0x24f && codePoint !== 0xd7 && codePoint !== 0xf7) ||
  (codePoint >= 0x400 && codePoint <= 0x4ff);

const CYRILLIC_IO = 0x451;
const CYRILLIC_IE = 0x435;

function lowercaseCodePoint(codePoint) {
  if (codePoint >= 0x41 && codePoint <= 0x5a) {
    return codePoint + 0x20;
  }

  if (codePoint >= 0xc0 && codePoint <= 0xde && codePoint !== 0xd7) {
    return codePoint + 0x20;
  }

  if (codePoint >= 0x410 && codePoint <= 0x42f) {
    return codePoint + 0x20;
  }

  const lowercased = codePoint >= 0x400 && codePoint <= 0x40f ? codePoint + 0x50 : codePoint;

  // ё is searched as е.
  return lowercased === CYRILLIC_IO ? CYRILLIC_IE : lowercased;
}

// Lowercased words: ASCII letters and digits, accented Latin, Cyrillic; ё folded into е.
export function searchWords(text) {
  const words = [];
  let word = '';
  for (const character of text) {
    const codePoint = lowercaseCodePoint(character.codePointAt(0));
    if (isWordCodePoint(codePoint)) {
      word += String.fromCodePoint(codePoint);
    } else if (word) {
      words.push(word);
      word = '';
    }
  }

  if (word) {
    words.push(word);
  }

  return words;
}

const utf8Hex = (text) => Buffer.from(text, 'utf8').toString('hex');

export function shardFileForQueryOrNull(query) {
  let longestWordLetters = [];
  for (const word of searchWords(query)) {
    const letters = [...word];
    if (letters.length > longestWordLetters.length) {
      longestWordLetters = letters;
    }
  }

  if (longestWordLetters.length < SHARD_MINIMUM_LETTERS) {
    return null;
  }

  return `${utf8Hex(longestWordLetters.slice(0, SHARD_LETTERS).join(''))}.json`;
}

// Those of the first letters of each of the item's words.
export function shardFilesForItem(name, shortName) {
  const shardFiles = new Set();
  for (const word of [...searchWords(name), ...searchWords(shortName)]) {
    const letters = [...word];
    for (let count = SHARD_MINIMUM_LETTERS; count <= Math.min(SHARD_LETTERS, letters.length); count += 1) {
      shardFiles.add(`${utf8Hex(letters.slice(0, count).join(''))}.json`);
    }
  }

  return shardFiles;
}

export function everyQueryWordStartsAWordOfTheItem(queryWords, name, shortName) {
  const itemWords = [...searchWords(name), ...searchWords(shortName)];

  return queryWords.every((queryWord) => itemWords.some((itemWord) => itemWord.startsWith(queryWord)));
}

// ---------------------------------------------------------------- building

const datasetData = (dataset) => dataset?.data ?? {};

function tasksTakingEachItem(tasks) {
  // In dataset order.
  const tasksByItemId = new Map();
  for (const task of Object.values(tasks)) {
    for (const objective of task.objectives ?? []) {
      if (!OBJECTIVES_TAKING_THE_ITEM.has(objective.type)) {
        continue;
      }

      for (const itemId of objective.items ?? []) {
        if (!tasksByItemId.has(itemId)) {
          tasksByItemId.set(itemId, new Map());
        }
        tasksByItemId.get(itemId).set(task.id, task);
      }
    }
  }

  return tasksByItemId;
}

// Shorter names first: "Graphics card" before the longer names that start the same.
function compareByNameLengthThenName(left, right) {
  if (left.name.length !== right.name.length) {
    return left.name.length - right.name.length;
  }

  if (left.name === right.name) {
    return 0;
  }

  return left.name < right.name ? -1 : 1;
}

/**
 * One game mode's datasets, as json.tarkov.dev serves them:
 * { items, tasks, traders, hideout, items_<lang>, tasks_<lang>, traders_<lang>, hideout_<lang> }.
 * Returns Map(path -> JSON document) for that mode.
 */
export function buildGameModeFiles(gameMode, datasets, { updated, minimumItemCount = DEFAULT_MINIMUM_ITEM_COUNT } = {}) {
  const items = datasetData(datasets.items).items ?? {};
  const fleaMarket = datasetData(datasets.items).fleaMarket ?? {};
  const tasks = datasetData(datasets.tasks).tasks ?? {};
  const traders = datasetData(datasets.traders);
  const stations = datasetData(datasets.hideout);
  const itemCount = Object.keys(items).length;
  if (itemCount < minimumItemCount) {
    throw new Error(`${gameMode}/items has ${itemCount} items, fewer than ${minimumItemCount}`);
  }

  const tasksByItemId = tasksTakingEachItem(tasks);
  const extensions = { source: SOURCE, updated };
  const files = new Map();

  for (const language of LANGUAGES) {
    // Missing translations fall back to English, then to the key itself.
    const translatorFor = (datasetName) => {
      const wanted = datasetData(datasets[`${datasetName}_${language}`]);
      const english = datasetData(datasets[`${datasetName}_en`]);

      return (key) => wanted[key] ?? english[key] ?? key;
    };
    const translateItem = translatorFor('items');
    const translateTask = translatorFor('tasks');
    const translateTrader = translatorFor('traders');
    const translateHideout = translatorFor('hideout');
    const fleaMarketName = translateItem(fleaMarket.name ?? 'FleaMarket');

    const records = [];
    for (const item of Object.values(items)) {
      const name = translateItem(item.name);
      // Untranslated placeholders are not real items.
      if (!name || name === item.name) {
        continue;
      }

      const sellFor = (item.sellToTrader ?? [])
        .filter((offer) => offer.priceRUB > 0 && traders[offer.trader])
        .map((offer) => {
          const trader = traders[offer.trader];
          const vendor = { name: translateTrader(trader.name), normalizedName: trader.normalizedName };

          return { priceRUB: offer.priceRUB, vendor };
        });
      // tarkov.dev's GraphQL API lists the last lowest offer as the flea's price.
      if (item.lastLowPrice > 0 && !(item.types ?? []).includes('noFlea')) {
        sellFor.push({ priceRUB: item.lastLowPrice, vendor: { name: fleaMarketName, normalizedName: FLEA_VENDOR } });
      }

      const usedInTasks = [...(tasksByItemId.get(item.id)?.values() ?? [])].map((task) => ({
        id: task.id,
        name: translateTask(task.name),
        minPlayerLevel: task.minPlayerLevel ?? 0,
      }));

      records.push({
        id: item.id,
        name,
        shortName: translateItem(item.shortName),
        iconLink: item.iconLink ?? null,
        basePrice: item.basePrice ?? 0,
        avg24hPrice: item.avg24hPrice ?? null,
        lastLowPrice: item.lastLowPrice ?? null,
        width: item.width ?? 1,
        height: item.height ?? 1,
        types: item.types ?? [],
        sellFor,
        usedInTasks,
      });
    }
    records.sort(compareByNameLengthThenName);

    const languageDirectory = `${gameMode}/${language}`;
    const recordsByShardFile = new Map();
    for (const record of records) {
      files.set(`${languageDirectory}/items/${record.id}.json`, { data: { items: [record] }, extensions });
      for (const shardFile of shardFilesForItem(record.name, record.shortName)) {
        if (!recordsByShardFile.has(shardFile)) {
          recordsByShardFile.set(shardFile, []);
        }
        recordsByShardFile.get(shardFile).push(record);
      }
    }
    for (const [shardFile, shardRecords] of recordsByShardFile) {
      files.set(`${languageDirectory}/search/${shardFile}`, { data: { items: shardRecords }, extensions });
    }

    const hideoutStations = Object.values(stations).map((station) => ({
      name: translateHideout(station.name),
      levels: (station.levels ?? []).map((level) => ({
        level: level.level,
        itemRequirements: (level.itemRequirements ?? []).map((requirement) => ({
          count: requirement.count,
          item: { id: requirement.item },
        })),
      })),
    }));
    files.set(`${languageDirectory}/hideout.json`, { data: { hideoutStations }, extensions });
  }

  return files;
}
