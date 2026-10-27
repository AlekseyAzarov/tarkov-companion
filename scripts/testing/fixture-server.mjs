// Serves contracts/fixtures (optionally mutated) so scripts/contract-check.mjs can be
// tested offline:  node scripts/testing/fixture-server.mjs [same|add|break] <port>
import http from 'node:http';
import { readFileSync } from 'node:fs';

const [mutation, port] = process.argv.slice(2);
const itemSearchResponse = JSON.parse(readFileSync('contracts/fixtures/item_search.json', 'utf8'));
const hideoutResponse = readFileSync('contracts/fixtures/hideout.json', 'utf8');

if (mutation === 'break') {
  // A removed field and a changed type: both must fail the check.
  for (const item of itemSearchResponse.data.items) {
    delete item.lastLowPrice;
    item.width = String(item.width);
  }
}

if (mutation === 'add') {
  // Upstream additions must pass.
  for (const item of itemSearchResponse.data.items) {
    item.brandNewField = { nested: 1 };
  }
}

http
  .createServer((request, response) => {
    let body = '';
    request.on('data', (chunk) => {
      body += chunk;
    });
    request.on('end', () => {
      const isHideoutQuery = JSON.parse(body).query.startsWith('query Hideout');
      response.setHeader('Content-Type', 'application/json');
      response.end(isHideoutQuery ? hideoutResponse : JSON.stringify(itemSearchResponse));
    });
  })
  .listen(Number(port));
