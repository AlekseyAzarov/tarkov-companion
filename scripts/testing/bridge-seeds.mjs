// Writes each web message of the bridge transcript to its own file, as a seed
// corpus for the bridge fuzzer (scripts/run-fuzzers.sh). A string message is
// sent as is, like core/tests/bridge_transcript_test.cpp does.
//   node scripts/testing/bridge-seeds.mjs <bridge_transcript.json> <output directory>
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const [transcriptPath, outputDirectory] = process.argv.slice(2);
const transcript = JSON.parse(readFileSync(transcriptPath, 'utf8'));

transcript.exchanges.forEach((exchange, index) => {
  const message = typeof exchange.web === 'string' ? exchange.web : JSON.stringify(exchange.web);
  writeFileSync(join(outputDirectory, `seed-${index}`), message);
});
