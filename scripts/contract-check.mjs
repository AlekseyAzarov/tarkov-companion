#!/usr/bin/env node
// Sends the SDK's queries (contracts/queries/) to tarkov.dev and checks that
// every field path in the fixtures still exists with a compatible type.
// Additions are reported; removals and type changes fail.
//
//   node scripts/contract-check.mjs            # check
//   node scripts/contract-check.mjs --record   # also save live responses to contracts/recorded/
//
// No dependencies: Node 18+ only.
import { mkdir, readFile, readdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const REPOSITORY_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ENDPOINT = process.env.TARKOV_COMPANION_ENDPOINT ?? 'https://api.tarkov.dev/graphql';
const recordLiveResponses = process.argv.includes('--record');
const REQUEST_TIMEOUT_MILLISECONDS = 30_000;
const HTTP_OK = 200;
const ERROR_PREVIEW_LENGTH = 200;
const MAXIMUM_LISTED_ADDITIONS = 5;
// exitCode values: CI tells "contract broken" apart from "API down".
const EXIT_CONTRACT_BROKEN = 1;
const EXIT_UPSTREAM_UNREACHABLE = 2;

const queryFiles = (await readdir(path.join(REPOSITORY_ROOT, 'contracts/queries'))).filter((file) =>
  file.endsWith('.graphql'),
);
const queriesByName = Object.fromEntries(
  await Promise.all(
    queryFiles.map(async (file) => [
      path.basename(file, '.graphql'),
      await readFile(path.join(REPOSITORY_ROOT, 'contracts/queries', file), 'utf8'),
    ]),
  ),
);
const readFixture = async (name) =>
  JSON.parse(await readFile(path.join(REPOSITORY_ROOT, 'contracts/fixtures', name), 'utf8'));

const fixtureItemIds = (await readFixture('item_search.json')).data.items.map((item) => item.id);

const testCases = [
  {
    name: 'item_search',
    query: 'item_search',
    variables: { name: 'salewa', lang: 'en', gameMode: 'regular', limit: 5 },
    fixture: 'item_search.json',
  },
  {
    name: 'item_by_id',
    query: 'item_by_id',
    variables: { ids: fixtureItemIds, lang: 'en', gameMode: 'regular' },
    fixture: 'item_search.json',
  },
  {
    name: 'item_by_id_pve',
    query: 'item_by_id',
    variables: { ids: fixtureItemIds, lang: 'en', gameMode: 'pve' },
    fixture: 'item_search.json',
  },
  { name: 'hideout', query: 'hideout', variables: { lang: 'en', gameMode: 'regular' }, fixture: 'hideout.json' },
];

// Array elements share one path ("[]").
function typesByFieldPath(value, fieldPath = '$', types = new Map()) {
  const type = value === null ? 'null' : Array.isArray(value) ? 'array' : typeof value;
  if (!types.has(fieldPath)) {
    types.set(fieldPath, new Set());
  }
  types.get(fieldPath).add(type);

  if (type === 'array') {
    for (const element of value) {
      typesByFieldPath(element, `${fieldPath}[]`, types);
    }
  }

  if (type === 'object') {
    for (const [key, child] of Object.entries(value)) {
      typesByFieldPath(child, `${fieldPath}.${key}`, types);
    }
  }

  return types;
}

function compareShapes(expected, actual) {
  const problems = [];
  for (const [fieldPath, types] of expected) {
    const liveTypes = actual.get(fieldPath);
    if (!liveTypes) {
      // An empty array upstream means its element paths can't be observed; not a removal.
      const parentArray = fieldPath.slice(0, fieldPath.lastIndexOf('[]') + 2);
      const parentArrayIsEmpty = actual.has(parentArray.slice(0, -2)) && !actual.has(parentArray);
      if (fieldPath.includes('[]') && parentArrayIsEmpty) {
        continue;
      }

      problems.push(`missing ${fieldPath}`);
      continue;
    }

    // null is compatible with anything (nullable fields), and vice versa.
    const concreteTypes = [...types].filter((type) => type !== 'null');
    const liveConcreteTypes = [...liveTypes].filter((type) => type !== 'null');
    const bothConcrete = concreteTypes.length > 0 && liveConcreteTypes.length > 0;
    if (bothConcrete && !concreteTypes.some((type) => liveConcreteTypes.includes(type))) {
      problems.push(`type of ${fieldPath}: fixture ${[...types]} vs live ${[...liveTypes]}`);
    }
  }

  const additions = [...actual.keys()].filter((fieldPath) => !expected.has(fieldPath));

  return { problems, additions };
}

async function postQuery(query, variables) {
  try {
    const response = await fetch(ENDPOINT, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'User-Agent': 'tarkov-companion-contract-check' },
      body: JSON.stringify({ query, variables }),
      signal: AbortSignal.timeout(REQUEST_TIMEOUT_MILLISECONDS),
    });

    return { status: response.status, body: await response.json().catch(() => null) };
  } catch (error) {
    return { status: 0, body: { errors: [String(error.cause?.code ?? error.message)] } };
  }
}

function describeAdditions(additions) {
  if (additions.length === 0) {
    return '';
  }

  const listed = additions.slice(0, MAXIMUM_LISTED_ADDITIONS).join(', ');
  const ellipsis = additions.length > MAXIMUM_LISTED_ADDITIONS ? ', …' : '';

  return ` (upstream added: ${listed}${ellipsis})`;
}

let contractBroken = false;
let unavailableCount = 0;
for (const testCase of testCases) {
  const { status, body } = await postQuery(queriesByName[testCase.query], testCase.variables);
  if (status !== HTTP_OK || !body?.data) {
    // Upstream outage is not a contract break; report it separately.
    const errorPreview = JSON.stringify(body?.errors ?? '').slice(0, ERROR_PREVIEW_LENGTH);
    console.log(`? ${testCase.name}: upstream unavailable (HTTP ${status}) ${errorPreview}`);
    unavailableCount++;
    continue;
  }

  if (body.errors) {
    console.log(`! ${testCase.name}: partial errors ${JSON.stringify(body.errors).slice(0, ERROR_PREVIEW_LENGTH)}`);
  }

  const expectedShape = typesByFieldPath(await readFixture(testCase.fixture));
  const { problems, additions } = compareShapes(expectedShape, typesByFieldPath(body));
  if (problems.length > 0) {
    contractBroken = true;
    console.log(`✗ ${testCase.name}`);
    for (const problem of problems) {
      console.log(`    ${problem}`);
    }
  } else {
    console.log(`✓ ${testCase.name}${describeAdditions(additions)}`);
  }

  if (recordLiveResponses) {
    const recordedDirectory = path.join(REPOSITORY_ROOT, 'contracts/recorded');
    await mkdir(recordedDirectory, { recursive: true });
    await writeFile(path.join(recordedDirectory, `${testCase.name}.json`), JSON.stringify(body, null, 2) + '\n');
  }
}

// exitCode rather than exit(): lets open keep-alive sockets close cleanly.
if (contractBroken) {
  process.exitCode = EXIT_CONTRACT_BROKEN;
} else if (unavailableCount === testCases.length) {
  process.exitCode = EXIT_UPSTREAM_UNREACHABLE;
}
