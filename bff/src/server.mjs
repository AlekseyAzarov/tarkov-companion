// Development backend, NOT for production: an in-memory OAuth 2.0 server with
// PKCE whose sign-in page accepts any name, favourites sync and the catalog.
//
// Endpoints:
//   GET  /authorize   sign-in page          POST /authorize  its form
//   POST /token       code and refresh grants (refresh tokens rotate)
//   POST /revoke      revoke a token and its whole family
//   GET  /me          the signed-in account (Bearer access token)
//   POST /favorites/sync  the player's favourites: send changes, get everyone else's
//   GET  /catalog/...  the static catalog built by scripts/build-catalog.mjs (docs/catalog.md),
//                      as GitHub Pages would serve it
import { createHash, randomBytes } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { resolve, sep } from 'node:path';
import { gzipSync } from 'node:zlib';
import { createServer as createHttpServer } from 'node:http';
import {
  BEARER_AUTHORIZATION_PREFIX,
  CACHE_CONTROL_NO_STORE,
  CONTENT_TYPE,
  HTTP_HEADER,
  HTTP_METHOD,
  HTTP_STATUS,
} from './http.mjs';
import {
  OAUTH_CODE_CHALLENGE_METHOD_S256,
  OAUTH_ERROR,
  OAUTH_GRANT_TYPE,
  OAUTH_PARAMETER,
  OAUTH_PATH,
  OAUTH_RESPONSE_TYPE_CODE,
  OAUTH_TOKEN_TYPE_BEARER,
} from './oauth.mjs';

// The same limits the SDK enforces (contracts/config-limits.json).
const LIMITS = JSON.parse(readFileSync(new URL('../../contracts/config-limits.json', import.meta.url), 'utf8'));
const FAVORITES_MAXIMUM = LIMITS.shared.favoritesMaximum;
// A change stamped further ahead than this is ignored: a device whose clock
// is in the future would otherwise win every conflict forever.
const MAXIMUM_CLOCK_AHEAD_MILLISECONDS = 5 * 60_000;
const ITEM_ID_PATTERN = /^[A-Za-z0-9_-]{1,64}$/;
const MAXIMUM_TEXT_LENGTH = 200;
const OPTIONAL_TEXT_FIELDS_OF_A_CHANGE = ['name', 'shortName', 'iconLink'];

export const DEMO_CLIENT_ID = 'tarkov-companion-demo';
export const DEMO_REDIRECT_URI = 'com.tarkovcompanion.hostgame://tarkov-companion-auth';

const AUTHORIZATION_CODE_LIFETIME_MILLISECONDS = 60_000;
const DEFAULT_ACCESS_TOKEN_LIFETIME_SECONDS = 600;
// A full favourites sync (200 changes with names) fits with room to spare.
const MAXIMUM_BODY_BYTES = 64 * 1024;
// base64url of 32 random bytes; RFC 7636 allows 43..128 characters.
const PKCE_CHALLENGE_LENGTH = 43;
const MAXIMUM_PLAYER_NAME_LENGTH = 40;
const DEFAULT_PLAYER_NAME = 'Demo player';
const TOKEN_BYTES = 32;
// The sign-in form's own fields, posted back with the authorization request.
const FORM_FIELD_PLAYER_NAME = 'name';
const FORM_FIELD_DECISION = 'decision';
const DECISION_ALLOW = 'allow';
const FAVORITES_SYNC_PATH = '/favorites/sync';
const CATALOG_PATH = '/catalog';
const CATALOG_FILE_EXTENSION = '.json';
const CATALOG_CACHE_CONTROL = 'max-age=600';
const GZIP_ENCODING = 'gzip';

const randomToken = () => randomBytes(TOKEN_BYTES).toString('base64url');
const sha256Base64Url = (text) => createHash('sha256').update(text).digest('base64url');

/**
 * @param {object} [options]
 * @param {Record<string, {redirectUris: string[]}>} [options.clients]
 * @param {number} [options.accessTokenLifetimeSeconds]
 * @param {() => number} [options.now] milliseconds since the epoch, injectable for tests
 * @param {string} [options.catalogDirectory] where /catalog/ is served from; without it, /catalog/ is 404
 */
export function createServer(options = {}) {
  const clients = options.clients ?? { [DEMO_CLIENT_ID]: { redirectUris: [DEMO_REDIRECT_URI] } };
  const accessTokenLifetimeSeconds = options.accessTokenLifetimeSeconds ?? DEFAULT_ACCESS_TOKEN_LIFETIME_SECONDS;
  const now = options.now ?? Date.now;
  const catalogDirectory = options.catalogDirectory ? resolve(options.catalogDirectory) : null;

  // code -> {clientId, redirectUri, challenge, account, expiresAt}
  const grantsByAuthorizationCode = new Map();
  // token -> {account, family, expiresAt}
  const grantsByAccessToken = new Map();
  // token -> {account, family, clientId, used}
  const grantsByRefreshToken = new Map();
  const revokedFamilies = new Set();

  function issueTokens(account, family, clientId) {
    const accessToken = randomToken();
    const refreshToken = randomToken();
    grantsByAccessToken.set(accessToken, { account, family, expiresAt: now() + accessTokenLifetimeSeconds * 1000 });
    grantsByRefreshToken.set(refreshToken, { account, family, clientId, used: false });

    return {
      access_token: accessToken,
      token_type: OAUTH_TOKEN_TYPE_BEARER,
      expires_in: accessTokenLifetimeSeconds,
      refresh_token: refreshToken,
    };
  }

  function accountForPlayerName(playerName) {
    const displayName = playerName.trim().slice(0, MAXIMUM_PLAYER_NAME_LENGTH) || DEFAULT_PLAYER_NAME;

    return { id: `acc_${sha256Base64Url(displayName.toLowerCase()).slice(0, 12)}`, displayName };
  }

  // ---------------------------------------------------------------- /authorize

  function parseAuthorizationRequest(parameters) {
    const client = clients[parameters.get(OAUTH_PARAMETER.clientId) ?? ''];
    const redirectUri = parameters.get(OAUTH_PARAMETER.redirectUri) ?? '';
    // Never redirect to a URI the client didn't register: show the error here.
    if (!client || !client.redirectUris.includes(redirectUri)) {
      return { fatalError: 'Unknown client or redirect URI.' };
    }

    const state = parameters.get(OAUTH_PARAMETER.state) ?? '';
    const challenge = parameters.get(OAUTH_PARAMETER.codeChallenge) ?? '';
    const valid =
      parameters.get(OAUTH_PARAMETER.responseType) === OAUTH_RESPONSE_TYPE_CODE &&
      parameters.get(OAUTH_PARAMETER.codeChallengeMethod) === OAUTH_CODE_CHALLENGE_METHOD_S256 &&
      challenge.length === PKCE_CHALLENGE_LENGTH &&
      state.length > 0;

    return { clientId: parameters.get(OAUTH_PARAMETER.clientId), redirectUri, state, challenge, valid };
  }

  function redirect(response, redirectUri, query) {
    const location = `${redirectUri}?${new URLSearchParams(query)}`;
    response
      .writeHead(HTTP_STATUS.found, {
        [HTTP_HEADER.location]: location,
        [HTTP_HEADER.cacheControl]: CACHE_CONTROL_NO_STORE,
      })
      .end();
  }

  function showSignInPage(response, authorization) {
    // The form posts back everything the GET had, so the POST is checked the same way.
    const fields = {
      [OAUTH_PARAMETER.responseType]: OAUTH_RESPONSE_TYPE_CODE,
      [OAUTH_PARAMETER.clientId]: authorization.clientId,
      [OAUTH_PARAMETER.redirectUri]: authorization.redirectUri,
      [OAUTH_PARAMETER.state]: authorization.state,
      [OAUTH_PARAMETER.codeChallenge]: authorization.challenge,
      [OAUTH_PARAMETER.codeChallengeMethod]: OAUTH_CODE_CHALLENGE_METHOD_S256,
    };
    const hiddenInputs = Object.entries(fields)
      .map(([name, value]) => `<input type="hidden" name="${name}" value="${escapeHtml(value)}">`)
      .join('');

    sendHtml(response, HTTP_STATUS.ok, `
      <h1>Sign in</h1>
      <p>Development identity server: any name signs you in.</p>
      <form method="post" action="/authorize">${hiddenInputs}
        <label>Player name <input name="${FORM_FIELD_PLAYER_NAME}" value="${DEFAULT_PLAYER_NAME}" maxlength="${MAXIMUM_PLAYER_NAME_LENGTH}" autofocus></label>
        <button name="${FORM_FIELD_DECISION}" value="${DECISION_ALLOW}">Sign in</button>
        <button name="${FORM_FIELD_DECISION}" value="deny">Cancel</button>
      </form>`);
  }

  function handleAuthorize(request, response, parameters) {
    const authorization = parseAuthorizationRequest(parameters);
    if (authorization.fatalError) {
      return sendHtml(response, HTTP_STATUS.badRequest, `<h1>Sign-in error</h1><p>${escapeHtml(authorization.fatalError)}</p>`);
    }

    if (!authorization.valid) {
      return redirect(response, authorization.redirectUri, {
        [OAUTH_PARAMETER.error]: OAUTH_ERROR.invalidRequest,
        [OAUTH_PARAMETER.state]: authorization.state,
      });
    }

    if (request.method === HTTP_METHOD.get) {
      return showSignInPage(response, authorization);
    }

    if (parameters.get(FORM_FIELD_DECISION) !== DECISION_ALLOW) {
      return redirect(response, authorization.redirectUri, {
        [OAUTH_PARAMETER.error]: OAUTH_ERROR.accessDenied,
        [OAUTH_PARAMETER.state]: authorization.state,
      });
    }

    const code = randomToken();
    grantsByAuthorizationCode.set(code, {
      clientId: authorization.clientId,
      redirectUri: authorization.redirectUri,
      challenge: authorization.challenge,
      account: accountForPlayerName(parameters.get(FORM_FIELD_PLAYER_NAME) ?? ''),
      expiresAt: now() + AUTHORIZATION_CODE_LIFETIME_MILLISECONDS,
    });
    redirect(response, authorization.redirectUri, {
      [OAUTH_PARAMETER.code]: code,
      [OAUTH_PARAMETER.state]: authorization.state,
    });
  }

  // ---------------------------------------------------------------- /token

  function exchangeAuthorizationCode(response, parameters) {
    const code = parameters.get(OAUTH_PARAMETER.code) ?? '';
    const grant = grantsByAuthorizationCode.get(code);
    // Single use, whatever happens next.
    grantsByAuthorizationCode.delete(code);

    const verifier = parameters.get(OAUTH_PARAMETER.codeVerifier) ?? '';
    const valid =
      grant &&
      grant.expiresAt > now() &&
      grant.clientId === parameters.get(OAUTH_PARAMETER.clientId) &&
      grant.redirectUri === parameters.get(OAUTH_PARAMETER.redirectUri) &&
      sha256Base64Url(verifier) === grant.challenge;
    if (!valid) {
      return sendOauthError(response, OAUTH_ERROR.invalidGrant);
    }

    const tokens = issueTokens(grant.account, randomToken(), grant.clientId);

    return sendJson(response, HTTP_STATUS.ok, tokens, { [HTTP_HEADER.cacheControl]: CACHE_CONTROL_NO_STORE });
  }

  function exchangeRefreshToken(response, parameters) {
    const refreshToken = parameters.get(OAUTH_PARAMETER.refreshToken) ?? '';
    const grant = grantsByRefreshToken.get(refreshToken);
    if (!grant || grant.clientId !== parameters.get(OAUTH_PARAMETER.clientId) || revokedFamilies.has(grant.family)) {
      return sendOauthError(response, OAUTH_ERROR.invalidGrant);
    }

    if (grant.used) {
      // A rotated-out refresh token came back: someone else holds a copy.
      // Revoke the whole family, so the thief's tokens die too (RFC 9700).
      revokedFamilies.add(grant.family);

      return sendOauthError(response, OAUTH_ERROR.invalidGrant);
    }

    grant.used = true;
    const tokens = issueTokens(grant.account, grant.family, grant.clientId);

    return sendJson(response, HTTP_STATUS.ok, tokens, { [HTTP_HEADER.cacheControl]: CACHE_CONTROL_NO_STORE });
  }

  function handleToken(response, parameters) {
    const grantType = parameters.get(OAUTH_PARAMETER.grantType);
    if (grantType === OAUTH_GRANT_TYPE.authorizationCode) {
      return exchangeAuthorizationCode(response, parameters);
    }

    if (grantType === OAUTH_GRANT_TYPE.refreshToken) {
      return exchangeRefreshToken(response, parameters);
    }

    return sendOauthError(response, OAUTH_ERROR.unsupportedGrantType);
  }

  // RFC 7009: answer 200 whether or not the token was known.
  function handleRevoke(response, parameters) {
    const token = parameters.get(OAUTH_PARAMETER.token) ?? '';
    const grant = grantsByRefreshToken.get(token) ?? grantsByAccessToken.get(token);
    if (grant) {
      revokedFamilies.add(grant.family);
    }

    response.writeHead(HTTP_STATUS.ok).end();
  }

  // ---------------------------------------------------------------- /me

  function grantForBearerTokenOrSendUnauthorized(request, response) {
    const header = request.headers.authorization ?? '';
    const grant = header.startsWith(BEARER_AUTHORIZATION_PREFIX)
      ? grantsByAccessToken.get(header.slice(BEARER_AUTHORIZATION_PREFIX.length))
      : undefined;
    if (!grant || grant.expiresAt <= now() || revokedFamilies.has(grant.family)) {
      response
        .writeHead(HTTP_STATUS.unauthorized, { [HTTP_HEADER.wwwAuthenticate]: 'Bearer error="invalid_token"' })
        .end();

      return undefined;
    }

    return grant;
  }

  function handleMe(request, response) {
    const grant = grantForBearerTokenOrSendUnauthorized(request, response);
    if (grant) {
      sendJson(response, HTTP_STATUS.ok, grant.account);
    }
  }

  // ---------------------------------------------------------------- /favorites/sync

  // account id -> Map(itemId -> record)
  const favoriteRecordsByAccountId = new Map();
  let lastFavoritesSequence = 0;

  const isShortText = (value) => typeof value === 'string' && value.length <= MAXIMUM_TEXT_LENGTH;
  const isValidChange = (change) =>
    change !== null &&
    typeof change === 'object' &&
    ITEM_ID_PATTERN.test(change.itemId ?? '') &&
    typeof change.favorite === 'boolean' &&
    Number.isInteger(change.changedAt) &&
    OPTIONAL_TEXT_FIELDS_OF_A_CHANGE.every((field) => change[field] === undefined || isShortText(change[field]));

  function isValidSyncRequest(syncRequest) {
    return (
      syncRequest !== null &&
      Number.isInteger(syncRequest.since) &&
      syncRequest.since >= 0 &&
      Array.isArray(syncRequest.changes) &&
      syncRequest.changes.length <= FAVORITES_MAXIMUM &&
      syncRequest.changes.every(isValidChange)
    );
  }

  function applyChange(records, change) {
    const existing = records.get(change.itemId);
    if (change.changedAt > now() + MAXIMUM_CLOCK_AHEAD_MILLISECONDS) {
      return;
    }

    // Ties: the server's record stays.
    if (existing && existing.changedAt >= change.changedAt) {
      return;
    }

    const favoriteCount = [...records.values()].filter((record) => record.favorite).length;
    if (change.favorite && !existing?.favorite && favoriteCount >= FAVORITES_MAXIMUM) {
      return;
    }

    records.set(change.itemId, {
      itemId: change.itemId,
      favorite: change.favorite,
      changedAt: change.changedAt,
      name: change.name ?? existing?.name ?? '',
      shortName: change.shortName ?? existing?.shortName ?? '',
      iconLink: change.iconLink ?? existing?.iconLink ?? '',
      sequence: ++lastFavoritesSequence,
    });
  }

  // Every record changed since the client's cursor, plus the server's record
  // for each item it sent (docs/favorites.md).
  function handleFavoritesSync(request, response, body) {
    const grant = grantForBearerTokenOrSendUnauthorized(request, response);
    if (!grant) {
      return;
    }

    let syncRequest;
    try {
      syncRequest = JSON.parse(body);
    } catch {
      return sendJson(response, HTTP_STATUS.badRequest, { error: OAUTH_ERROR.invalidRequest });
    }

    if (!isValidSyncRequest(syncRequest)) {
      return sendJson(response, HTTP_STATUS.badRequest, { error: OAUTH_ERROR.invalidRequest });
    }

    const records = favoriteRecordsByAccountId.get(grant.account.id) ?? new Map();
    favoriteRecordsByAccountId.set(grant.account.id, records);
    const itemIdsSentByClient = new Set();
    for (const change of syncRequest.changes) {
      itemIdsSentByClient.add(change.itemId);
      applyChange(records, change);
    }

    const changes = [...records.values()]
      .filter((record) => record.sequence > syncRequest.since || itemIdsSentByClient.has(record.itemId))
      .map(({ sequence, ...record }) => record);
    // A change that was refused and had no record yet (over the limit, clock
    // too far ahead): tell the client it isn't a favourite, so it converges.
    for (const itemId of itemIdsSentByClient) {
      if (!records.has(itemId)) {
        changes.push({ itemId, favorite: false, changedAt: now() });
      }
    }

    sendJson(response, HTTP_STATUS.ok, { changes, cursor: lastFavoritesSequence });
  }

  // ---------------------------------------------------------------- /catalog/

  async function handleCatalog(request, response, pathname) {
    const relativePath = decodeURIComponent(pathname.slice(CATALOG_PATH.length));
    const file = catalogDirectory && resolve(catalogDirectory, `.${relativePath}`);
    // Only JSON files inside the catalog: no "..", no other files on this machine.
    if (!file || !file.startsWith(catalogDirectory + sep) || !file.endsWith(CATALOG_FILE_EXTENSION)) {
      return response.writeHead(HTTP_STATUS.notFound).end();
    }

    let content;
    try {
      content = await readFile(file);
    } catch {
      return response.writeHead(HTTP_STATUS.notFound).end();
    }

    const headers = { [HTTP_HEADER.contentType]: CONTENT_TYPE.json, [HTTP_HEADER.cacheControl]: CATALOG_CACHE_CONTROL };
    if (/\bgzip\b/.test(request.headers['accept-encoding'] ?? '')) {
      response.writeHead(HTTP_STATUS.ok, { ...headers, [HTTP_HEADER.contentEncoding]: GZIP_ENCODING }).end(gzipSync(content));
    } else {
      response.writeHead(HTTP_STATUS.ok, headers).end(content);
    }
  }

  async function route(request, response) {
    const url = new URL(request.url ?? '/', 'http://localhost');
    const isGet = request.method === HTTP_METHOD.get;
    const isPost = request.method === HTTP_METHOD.post;
    const body = isPost ? await readBody(request) : '';

    if (url.pathname === FAVORITES_SYNC_PATH && isPost) {
      return handleFavoritesSync(request, response, body);
    }

    if (url.pathname.startsWith(`${CATALOG_PATH}/`) && isGet) {
      return handleCatalog(request, response, url.pathname);
    }

    const parameters = isPost ? new URLSearchParams(body) : url.searchParams;
    if (url.pathname === OAUTH_PATH.authorize && (isGet || isPost)) {
      return handleAuthorize(request, response, parameters);
    }

    if (url.pathname === OAUTH_PATH.token && isPost) {
      return handleToken(response, parameters);
    }

    if (url.pathname === OAUTH_PATH.revoke && isPost) {
      return handleRevoke(response, parameters);
    }

    if (url.pathname === OAUTH_PATH.account && isGet) {
      return handleMe(request, response);
    }

    response.writeHead(HTTP_STATUS.notFound).end();
  }

  return createHttpServer(async (request, response) => {
    try {
      await route(request, response);
    } catch (error) {
      response.writeHead(error instanceof BodyTooLargeError ? HTTP_STATUS.payloadTooLarge : HTTP_STATUS.badRequest).end();
    }
  });
}

class BodyTooLargeError extends Error {}

function readBody(request) {
  return new Promise((resolve, reject) => {
    let receivedBytes = 0;
    const chunks = [];

    request.on('data', (chunk) => {
      receivedBytes += chunk.length;
      if (receivedBytes > MAXIMUM_BODY_BYTES) {
        reject(new BodyTooLargeError());
        request.destroy();
        return;
      }

      chunks.push(chunk);
    });
    request.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    request.on('error', reject);
  });
}

function sendJson(response, status, body, headers = {}) {
  response.writeHead(status, { [HTTP_HEADER.contentType]: CONTENT_TYPE.json, ...headers }).end(JSON.stringify(body));
}

function sendOauthError(response, error) {
  sendJson(response, HTTP_STATUS.badRequest, { error }, { [HTTP_HEADER.cacheControl]: CACHE_CONTROL_NO_STORE });
}

function sendHtml(response, status, body) {
  response
    .writeHead(status, {
      [HTTP_HEADER.contentType]: CONTENT_TYPE.html,
      [HTTP_HEADER.cacheControl]: CACHE_CONTROL_NO_STORE,
      // No form-action: browsers apply it to the redirect after the form
      // posts, which here goes to the app's own URI scheme.
      [HTTP_HEADER.contentSecurityPolicy]: "default-src 'none'; style-src 'unsafe-inline'",
      [HTTP_HEADER.frameOptions]: 'DENY',
    })
    .end(`<!doctype html><meta name="viewport" content="width=device-width"><title>Sign in</title>
<style>body{font:16px system-ui;max-width:28em;margin:2em auto;padding:0 1em}input,button{font:inherit;padding:.5em}</style>${body}`);
}

function escapeHtml(text) {
  return String(text).replace(/[&<>"']/g, (character) => `&#${character.charCodeAt(0)};`);
}
