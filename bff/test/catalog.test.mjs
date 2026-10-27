import assert from 'node:assert/strict';
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { request } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { gunzipSync } from 'node:zlib';
import { after, before, describe, it } from 'node:test';
import { createServer } from '../src/server.mjs';

const graphicsCardResponse = { data: { items: [{ id: 'gpu', name: 'Graphics card' }] } };
let temporaryDirectory;
let server;
let port;

before(async () => {
  temporaryDirectory = mkdtempSync(join(tmpdir(), 'catalog-'));
  const itemsDirectory = join(temporaryDirectory, 'catalog', 'regular', 'en', 'items');
  mkdirSync(itemsDirectory, { recursive: true });
  writeFileSync(join(itemsDirectory, 'gpu.json'), JSON.stringify(graphicsCardResponse));
  writeFileSync(join(temporaryDirectory, 'secret.json'), '{"secret":true}');
  writeFileSync(join(temporaryDirectory, 'catalog', 'notes.txt'), 'not json');

  server = createServer({ catalogDirectory: join(temporaryDirectory, 'catalog') });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  port = server.address().port;
});

after(() => {
  server.close();
  rmSync(temporaryDirectory, { recursive: true, force: true });
});

// node:http rather than fetch: fetch normalizes "..", and those paths are what is tested here.
function get(path, headers = {}) {
  return new Promise((resolve, reject) => {
    request({ port, path, headers }, (response) => {
      const chunks = [];
      response.on('data', (chunk) => chunks.push(chunk));
      response.on('end', () => {
        resolve({ status: response.statusCode, headers: response.headers, body: Buffer.concat(chunks) });
      });
    })
      .on('error', reject)
      .end();
  });
}

describe('/catalog/', () => {
  it('serves catalog files as JSON', async () => {
    const response = await get('/catalog/regular/en/items/gpu.json');

    assert.equal(response.status, 200);
    assert.equal(response.headers['content-type'], 'application/json');
    assert.deepEqual(JSON.parse(response.body), graphicsCardResponse);
  });

  it('compresses when the client accepts gzip, as a CDN would', async () => {
    const response = await get('/catalog/regular/en/items/gpu.json', { 'Accept-Encoding': 'gzip' });

    assert.equal(response.headers['content-encoding'], 'gzip');
    assert.deepEqual(JSON.parse(gunzipSync(response.body)), graphicsCardResponse);
  });

  it('serves nothing outside the catalog, and only JSON', async () => {
    assert.equal((await get('/catalog/../secret.json')).status, 404);
    assert.equal((await get('/catalog/%2e%2e/secret.json')).status, 404);
    assert.equal((await get('/catalog/notes.txt')).status, 404);
    assert.equal((await get('/catalog/regular/en/items/missing.json')).status, 404);
  });

  it('is absent without a catalog directory', async () => {
    const serverWithoutCatalog = createServer();
    await new Promise((resolve) => serverWithoutCatalog.listen(0, '127.0.0.1', resolve));

    const url = `http://127.0.0.1:${serverWithoutCatalog.address().port}/catalog/regular/en/items/gpu.json`;
    const response = await fetch(url);

    assert.equal(response.status, 404);
    serverWithoutCatalog.close();
  });
});
