import assert from 'node:assert/strict';
import { createHash, randomBytes } from 'node:crypto';
import { after, before, beforeEach, describe, it } from 'node:test';
import { createServer, DEMO_CLIENT_ID, DEMO_REDIRECT_URI } from '../src/server.mjs';

let clock = 1_000_000_000;
let server;
let baseUrl;

before(async () => {
  server = createServer({ now: () => clock });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(() => server.close());

beforeEach(() => {
  clock += 1_000_000;
});

async function accessTokenFor(playerName) {
  const verifier = randomBytes(32).toString('base64url');
  const authorizeFields = {
    response_type: 'code',
    client_id: DEMO_CLIENT_ID,
    redirect_uri: DEMO_REDIRECT_URI,
    state: 's',
    code_challenge: createHash('sha256').update(verifier).digest('base64url'),
    code_challenge_method: 'S256',
    name: playerName,
    decision: 'allow',
  };
  const authorizeResponse = await fetch(`${baseUrl}/authorize`, {
    method: 'POST',
    body: new URLSearchParams(authorizeFields),
    redirect: 'manual',
  });
  const code = new URL(authorizeResponse.headers.get('location')).searchParams.get('code');

  const tokenResponse = await fetch(`${baseUrl}/token`, {
    method: 'POST',
    body: new URLSearchParams({
      grant_type: 'authorization_code',
      code,
      code_verifier: verifier,
      client_id: DEMO_CLIENT_ID,
      redirect_uri: DEMO_REDIRECT_URI,
    }),
  });
  const { access_token: accessToken } = await tokenResponse.json();

  return accessToken;
}

// A signed-in "device": its own tokens for the named account.
async function signedInDevice(playerName) {
  const accessToken = await accessTokenFor(playerName);
  let cursor = 0;

  return {
    async sync(changes = []) {
      const response = await fetch(`${baseUrl}/favorites/sync`, {
        method: 'POST',
        headers: { Authorization: `Bearer ${accessToken}`, 'Content-Type': 'application/json' },
        body: JSON.stringify({ since: cursor, changes }),
      });
      const body = await response.json();
      if (!response.ok) {
        throw new Error(`sync failed: ${response.status} ${body.error}`);
      }

      cursor = body.cursor;

      return body.changes;
    },
  };
}

const favoriteChange = (itemId, changedAt, favorite = true) => ({ itemId, favorite, changedAt, name: `Item ${itemId}` });
const changesByItemId = (changes) => Object.fromEntries(changes.map((change) => [change.itemId, change]));

describe('favourites sync', () => {
  it('a change made on one device reaches the other', async () => {
    const phone = await signedInDevice('Sync Player 1');
    const tablet = await signedInDevice('Sync Player 1');
    await tablet.sync();

    await phone.sync([favoriteChange('gpu', clock)]);
    const received = changesByItemId(await tablet.sync());

    assert.equal(received.gpu.favorite, true);
    assert.equal(received.gpu.name, 'Item gpu');
    assert.deepEqual(await tablet.sync(), [], 'nothing new since the cursor');
  });

  it('per item, the later change wins, whatever order the syncs arrive in', async () => {
    const phone = await signedInDevice('Sync Player 2');
    const tablet = await signedInDevice('Sync Player 2');

    // The phone unstarred at 10:00 while offline; the tablet starred at 11:00 and synced first.
    await tablet.sync([favoriteChange('gpu', clock - 1_000)]);
    const answer = changesByItemId(await phone.sync([favoriteChange('gpu', clock - 2_000, false)]));

    assert.equal(answer.gpu.favorite, true, 'the server answers with the record that won');
  });

  it('removals are kept, so an offline device learns about them', async () => {
    const phone = await signedInDevice('Sync Player 3');
    const tablet = await signedInDevice('Sync Player 3');
    await phone.sync([favoriteChange('gpu', clock - 2_000)]);
    await tablet.sync();

    await phone.sync([favoriteChange('gpu', clock - 1_000, false)]);

    assert.equal(changesByItemId(await tablet.sync()).gpu.favorite, false);
  });

  it('a change stamped far in the future is refused', async () => {
    const phone = await signedInDevice('Sync Player 4');

    const answer = changesByItemId(await phone.sync([favoriteChange('gpu', clock + 60 * 60_000)]));

    assert.equal(answer.gpu.favorite, false);
  });

  it('accounts are separate', async () => {
    const mine = await signedInDevice('Sync Player 5');
    const theirs = await signedInDevice('Sync Player 6');

    await mine.sync([favoriteChange('gpu', clock)]);

    assert.deepEqual(await theirs.sync(), []);
  });

  it('caps the list at the contract limit', async () => {
    const phone = await signedInDevice('Sync Player 7');
    const fullList = Array.from({ length: 200 }, (_, index) => favoriteChange(`item${index}`, clock - 1_000));
    await phone.sync(fullList);

    const answer = changesByItemId(await phone.sync([favoriteChange('one-more', clock)]));

    assert.equal(answer['one-more'].favorite, false);
  });

  it('needs a valid token and a well-formed body', async () => {
    const anonymous = await fetch(`${baseUrl}/favorites/sync`, { method: 'POST', body: '{"since":0,"changes":[]}' });
    assert.equal(anonymous.status, 401);

    const phone = await signedInDevice('Sync Player 8');
    await assert.rejects(phone.sync([{ itemId: '../x', favorite: true, changedAt: 1 }]));
  });
});
