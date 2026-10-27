import assert from 'node:assert/strict';
import { createHash, randomBytes } from 'node:crypto';
import { after, before, beforeEach, describe, it } from 'node:test';
import { createServer, DEMO_CLIENT_ID, DEMO_REDIRECT_URI } from '../src/server.mjs';

const ACCESS_TOKEN_LIFETIME_SECONDS = 60;

let clock = 1_000_000;
let server;
let baseUrl;

before(async () => {
  server = createServer({ accessTokenLifetimeSeconds: ACCESS_TOKEN_LIFETIME_SECONDS, now: () => clock });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  baseUrl = `http://127.0.0.1:${server.address().port}`;
});

after(() => server.close());

beforeEach(() => {
  clock += 1_000_000;
});

const verifierAndChallenge = () => {
  const verifier = randomBytes(32).toString('base64url');

  return { verifier, challenge: createHash('sha256').update(verifier).digest('base64url') };
};

const formPost = (fields) => ({
  method: 'POST',
  headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
  body: new URLSearchParams(fields),
  redirect: 'manual',
});

const authorizeFields = (challenge, overrides = {}) => ({
  response_type: 'code',
  client_id: DEMO_CLIENT_ID,
  redirect_uri: DEMO_REDIRECT_URI,
  state: 'state-123',
  code_challenge: challenge,
  code_challenge_method: 'S256',
  ...overrides,
});

const authorizeUrl = (fields) => `${baseUrl}/authorize?${new URLSearchParams(fields)}`;

async function signIn(playerName = 'Nikita', pkce = verifierAndChallenge()) {
  const fields = { ...authorizeFields(pkce.challenge), name: playerName, decision: 'allow' };
  const response = await fetch(`${baseUrl}/authorize`, formPost(fields));
  assert.equal(response.status, 302);
  const location = new URL(response.headers.get('location'));

  return { ...pkce, code: location.searchParams.get('code'), state: location.searchParams.get('state') };
}

async function exchangeCode(code, verifier, overrides = {}) {
  const fields = {
    grant_type: 'authorization_code',
    code,
    code_verifier: verifier,
    client_id: DEMO_CLIENT_ID,
    redirect_uri: DEMO_REDIRECT_URI,
    ...overrides,
  };
  const response = await fetch(`${baseUrl}/token`, formPost(fields));

  return { status: response.status, body: await response.json() };
}

async function refresh(refreshToken) {
  const fields = { grant_type: 'refresh_token', refresh_token: refreshToken, client_id: DEMO_CLIENT_ID };
  const response = await fetch(`${baseUrl}/token`, formPost(fields));

  return { status: response.status, body: await response.json() };
}

const fetchMe = (accessToken) => fetch(`${baseUrl}/me`, { headers: { Authorization: `Bearer ${accessToken}` } });

describe('authorization code flow with PKCE', () => {
  it('shows a sign-in page, then redirects back with the code and the state', async () => {
    const page = await fetch(authorizeUrl(authorizeFields(verifierAndChallenge().challenge)));
    assert.equal(page.status, 200);
    assert.match(await page.text(), /<form method="post"/);

    const { code, state } = await signIn();
    assert.ok(code);
    assert.equal(state, 'state-123');
  });

  it('the page itself posts everything the sign-in needs (as a browser would)', async () => {
    const { challenge } = verifierAndChallenge();
    const page = await (await fetch(authorizeUrl(authorizeFields(challenge)))).text();
    const hiddenFields = Object.fromEntries(
      [...page.matchAll(/<input type="hidden" name="([^"]+)" value="([^"]*)">/g)].map(([, name, value]) => [name, value]),
    );

    const response = await fetch(`${baseUrl}/authorize`, formPost({ ...hiddenFields, name: 'Nikita', decision: 'allow' }));

    const location = new URL(response.headers.get('location'));
    assert.ok(location.searchParams.get('code'), `expected a code, got ${location}`);
    assert.equal(location.searchParams.get('state'), 'state-123');
  });

  it('exchanges the code for tokens that open /me', async () => {
    const { code, verifier } = await signIn('Nikita');

    const { status, body } = await exchangeCode(code, verifier);

    assert.equal(status, 200);
    assert.equal(body.token_type, 'Bearer');
    assert.equal(body.expires_in, ACCESS_TOKEN_LIFETIME_SECONDS);

    const account = await (await fetchMe(body.access_token)).json();
    assert.equal(account.displayName, 'Nikita');
    assert.match(account.id, /^acc_/);
  });

  it('rejects a wrong verifier, and a code used twice', async () => {
    const { code, verifier } = await signIn();

    assert.equal((await exchangeCode(code, 'x'.repeat(43))).body.error, 'invalid_grant');
    assert.equal((await exchangeCode(code, verifier)).body.error, 'invalid_grant', 'the failed attempt burnt the code');
  });

  it('never redirects to an unregistered URI', async () => {
    const { challenge } = verifierAndChallenge();
    const fields = authorizeFields(challenge, { redirect_uri: 'evil://steal' });

    const response = await fetch(authorizeUrl(fields), { redirect: 'manual' });

    assert.equal(response.status, 400);
    assert.equal(response.headers.get('location'), null);
  });

  it('requires S256 PKCE', async () => {
    const fields = authorizeFields('plain', { code_challenge_method: 'plain' });

    const response = await fetch(authorizeUrl(fields), { redirect: 'manual' });

    assert.equal(new URL(response.headers.get('location')).searchParams.get('error'), 'invalid_request');
  });

  it('reports a cancelled sign-in as access_denied', async () => {
    const { challenge } = verifierAndChallenge();

    const response = await fetch(`${baseUrl}/authorize`, formPost({ ...authorizeFields(challenge), decision: 'deny' }));

    const location = new URL(response.headers.get('location'));
    assert.equal(location.searchParams.get('error'), 'access_denied');
    assert.equal(location.searchParams.get('state'), 'state-123');
  });
});

describe('tokens', () => {
  it('expire, and /me then answers 401', async () => {
    const { code, verifier } = await signIn();
    const { body } = await exchangeCode(code, verifier);

    clock += (ACCESS_TOKEN_LIFETIME_SECONDS + 1) * 1000;
    const response = await fetchMe(body.access_token);

    assert.equal(response.status, 401);
    assert.match(response.headers.get('www-authenticate'), /invalid_token/);
  });

  it('refresh tokens rotate', async () => {
    const { code, verifier } = await signIn();
    const first = (await exchangeCode(code, verifier)).body;

    const second = await refresh(first.refresh_token);

    assert.equal(second.status, 200);
    assert.notEqual(second.body.refresh_token, first.refresh_token);
    assert.equal((await fetchMe(second.body.access_token)).status, 200);
  });

  it('a reused refresh token revokes the whole family', async () => {
    const { code, verifier } = await signIn();
    const first = (await exchangeCode(code, verifier)).body;
    const second = (await refresh(first.refresh_token)).body;

    assert.equal((await refresh(first.refresh_token)).body.error, 'invalid_grant', 'replay of a rotated token');
    assert.equal((await fetchMe(second.access_token)).status, 401, 'the legitimate holder is signed out too');
    assert.equal((await refresh(second.refresh_token)).body.error, 'invalid_grant');
  });

  it('revoke ends the session', async () => {
    const { code, verifier } = await signIn();
    const tokens = (await exchangeCode(code, verifier)).body;

    const response = await fetch(`${baseUrl}/revoke`, formPost({ token: tokens.refresh_token }));

    assert.equal(response.status, 200);
    assert.equal((await fetchMe(tokens.access_token)).status, 401);
    assert.equal((await fetch(`${baseUrl}/revoke`, formPost({ token: 'unknown' }))).status, 200, 'RFC 7009: always 200');
  });
});
