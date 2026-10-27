// Runs the development backend:
//
//   node bff/src/main.mjs [--port 8787] [--access-token-lifetime 600] [--catalog build/catalog]
//
// The Android emulator reaches it as http://10.0.2.2:8787 (debug builds of
// the sample game point there; see docs/auth.md). --catalog serves a catalog
// built by scripts/build-catalog.mjs (docs/catalog.md).
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createServer } from './server.mjs';

const DEFAULT_PORT = 8787;
const DEFAULT_CATALOG_DIRECTORY = join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'build', 'catalog');

function commandLineOption(name, fallback) {
  const index = process.argv.indexOf(`--${name}`);

  return index >= 0 ? process.argv[index + 1] : fallback;
}

const port = Number(commandLineOption('port', DEFAULT_PORT));
const accessTokenLifetimeOption = commandLineOption('access-token-lifetime', undefined);
const accessTokenLifetimeSeconds =
  accessTokenLifetimeOption === undefined ? undefined : Number(accessTokenLifetimeOption);
const catalogDirectory = commandLineOption('catalog', DEFAULT_CATALOG_DIRECTORY);

const server = createServer({ accessTokenLifetimeSeconds, catalogDirectory });

// One line per request: method, path (no query: it may hold a code) and status.
server.on('request', (request, response) => {
  response.on('finish', () => {
    console.log(`${request.method} ${request.url?.split('?')[0]} -> ${response.statusCode}`);
  });
});

server.listen(port, '127.0.0.1', () => {
  console.log(`Development backend on http://127.0.0.1:${port} (emulator: http://10.0.2.2:${port})`);
  console.log(`Catalog from ${catalogDirectory}`);
});
