// OAuth 2.0 names (RFC 6749, 7636, 7009); the SDK's side is core/src/oauth.h.

export const OAUTH_PATH = {
  authorize: '/authorize',
  token: '/token',
  revoke: '/revoke',
  account: '/me',
};

export const OAUTH_PARAMETER = {
  responseType: 'response_type',
  clientId: 'client_id',
  redirectUri: 'redirect_uri',
  state: 'state',
  codeChallenge: 'code_challenge',
  codeChallengeMethod: 'code_challenge_method',
  code: 'code',
  codeVerifier: 'code_verifier',
  grantType: 'grant_type',
  refreshToken: 'refresh_token',
  token: 'token',
  error: 'error',
};

export const OAUTH_RESPONSE_TYPE_CODE = 'code';
export const OAUTH_CODE_CHALLENGE_METHOD_S256 = 'S256';
export const OAUTH_TOKEN_TYPE_BEARER = 'Bearer';

export const OAUTH_GRANT_TYPE = {
  authorizationCode: 'authorization_code',
  refreshToken: 'refresh_token',
};

export const OAUTH_ERROR = {
  invalidRequest: 'invalid_request',
  invalidGrant: 'invalid_grant',
  unsupportedGrantType: 'unsupported_grant_type',
  accessDenied: 'access_denied',
};
