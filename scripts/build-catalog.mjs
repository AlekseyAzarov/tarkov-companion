#!/usr/bin/env node
// Builds the static catalog (docs/catalog.md) from json.tarkov.dev.
//
//   <out>/manifest.json
//   <out>/<game mode>/<language>/items/<item id>.json   ItemById response
//   <out>/<game mode>/<language>/search/<shard>.json    ItemSearch response: every item with a
//                                                       word starting with the shard's letters
//   <out>/<game mode>/<language>/hideout.json           Hideout response
//
// Usage: node scripts/build-catalog.mjs [--out build/catalog] [--from <dir>] [--max-age <minutes>]
//   --from     read <mode>_<dataset>.json files from a directory instead of json.tarkov.dev
//   --max-age  keep the catalog in <out> if it was built less than this long ago
import { existsSync } from 'node:fs';
import { mkdir, readFile, rename, rm, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { GAME_MODES, LANGUAGES, SOURCE, buildGameModeFiles } from './catalog/catalog.mjs';

const DATASETS = ['items', 'tasks', 'traders', 'hideout'];
const MILLISECONDS_PER_MINUTE = 60_000;

async function loadGameModeDatasets(gameMode, localDirectoryOrNull) {
  const translatedDatasets = DATASETS.flatMap((dataset) => LANGUAGES.map((language) => `${dataset}_${language}`));
  const datasets = {};

  for (const name of [...DATASETS, ...translatedDatasets]) {
    if (localDirectoryOrNull) {
      const file = join(localDirectoryOrNull, `${gameMode}_${name}.json`);
      datasets[name] = existsSync(file) ? JSON.parse(await readFile(file, 'utf8')) : {};
      continue;
    }

    const url = `${SOURCE}/${gameMode}/${name}`;
    const response = await fetch(url, { headers: { 'User-Agent': 'tarkov-companion-catalog' } });
    if (!response.ok) {
      throw new Error(`${url}: HTTP ${response.status}`);
    }

    datasets[name] = await response.json();
  }

  return datasets;
}

async function catalogAgeInMinutesOrNull(outputDirectory) {
  const manifestFile = join(outputDirectory, 'manifest.json');
  if (!existsSync(manifestFile)) {
    return null;
  }

  const builtAt = Date.parse(JSON.parse(await readFile(manifestFile, 'utf8')).updated);

  return (Date.now() - builtAt) / MILLISECONDS_PER_MINUTE;
}

async function main(commandLineArguments) {
  const option = (name, fallback) => {
    const index = commandLineArguments.indexOf(`--${name}`);

    return index >= 0 ? commandLineArguments[index + 1] : fallback;
  };
  const repositoryRoot = join(dirname(fileURLToPath(import.meta.url)), '..');
  const outputDirectory = option('out', join(repositoryRoot, 'build', 'catalog'));
  const localDatasetDirectory = option('from', null);
  const maximumAgeMinutes = Number(option('max-age', 0));
  const updated = new Date().toISOString();

  if (maximumAgeMinutes > 0) {
    const ageMinutes = await catalogAgeInMinutesOrNull(outputDirectory);
    if (ageMinutes !== null && ageMinutes >= 0 && ageMinutes < maximumAgeMinutes) {
      console.log(`catalog in ${outputDirectory} is ${Math.round(ageMinutes)} minutes old: kept`);
      return;
    }
  }

  // Built next to the target and swapped in at the end: a failed run leaves the previous catalog.
  const stagingDirectory = `${outputDirectory}.staging`;
  await rm(stagingDirectory, { recursive: true, force: true });

  let fileCount = 0;
  for (const gameMode of GAME_MODES) {
    const datasets = await loadGameModeDatasets(gameMode, localDatasetDirectory);
    const files = buildGameModeFiles(gameMode, datasets, { updated });
    for (const [path, document] of files) {
      const file = join(stagingDirectory, path);
      await mkdir(dirname(file), { recursive: true });
      await writeFile(file, JSON.stringify(document));
    }

    fileCount += files.size;
    console.log(`${gameMode}: ${files.size} files`);
  }

  const manifest = {
    schema: 1,
    updated,
    source: SOURCE,
    gameModes: GAME_MODES,
    languages: LANGUAGES,
    files: fileCount,
  };
  await writeFile(join(stagingDirectory, 'manifest.json'), JSON.stringify(manifest, null, 2));
  await replaceDirectoryKeepingTheOldOneOnFailure(stagingDirectory, outputDirectory);
  console.log(`catalog: ${fileCount} files in ${outputDirectory}`);
}

// The previous catalog moves aside, not away, until the new one is in place:
// if the swap fails, the previous one is put back rather than lost.
async function replaceDirectoryKeepingTheOldOneOnFailure(newDirectory, targetDirectory) {
  const previousDirectory = `${targetDirectory}.previous`;
  await rm(previousDirectory, { recursive: true, force: true });
  const hadPrevious = existsSync(targetDirectory);
  if (hadPrevious) {
    await renameRetryingWhileWindowsHoldsTheFiles(targetDirectory, previousDirectory);
  }

  try {
    await renameRetryingWhileWindowsHoldsTheFiles(newDirectory, targetDirectory);
  } catch (error) {
    if (hadPrevious) {
      await renameRetryingWhileWindowsHoldsTheFiles(previousDirectory, targetDirectory);
    }
    throw new Error(`could not move the new catalog into ${targetDirectory} (${error.code}); kept the previous one`);
  }

  await rm(previousDirectory, { recursive: true, force: true });
}

// Windows refuses to rename a directory for a while after thousands of files
// were written into it: antivirus and the search indexer still have them open.
const TRANSIENT_RENAME_ERRORS = new Set(['EPERM', 'EBUSY', 'EACCES']);
const RENAME_ATTEMPTS = 10;
const RENAME_RETRY_STEP_MILLISECONDS = 500;

async function renameRetryingWhileWindowsHoldsTheFiles(from, to) {
  for (let attempt = 1; ; attempt += 1) {
    try {
      await rename(from, to);
      return;
    } catch (error) {
      if (!TRANSIENT_RENAME_ERRORS.has(error.code) || attempt === RENAME_ATTEMPTS) {
        throw error;
      }

      await new Promise((resolve) => setTimeout(resolve, RENAME_RETRY_STEP_MILLISECONDS * attempt));
    }
  }
}

main(process.argv.slice(2)).catch((error) => {
  console.error(error.message);
  process.exit(1);
});
