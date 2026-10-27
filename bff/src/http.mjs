// HTTP names and status codes the development backend uses (RFC 9110).

export const HTTP_STATUS = {
  ok: 200,
  found: 302,
  badRequest: 400,
  unauthorized: 401,
  notFound: 404,
  payloadTooLarge: 413,
};

export const HTTP_METHOD = {
  get: 'GET',
  post: 'POST',
};

export const HTTP_HEADER = {
  cacheControl: 'Cache-Control',
  contentEncoding: 'Content-Encoding',
  contentSecurityPolicy: 'Content-Security-Policy',
  contentType: 'Content-Type',
  frameOptions: 'X-Frame-Options',
  location: 'Location',
  wwwAuthenticate: 'WWW-Authenticate',
};

export const CONTENT_TYPE = {
  json: 'application/json',
  html: 'text/html; charset=utf-8',
};

export const CACHE_CONTROL_NO_STORE = 'no-store';
export const BEARER_AUTHORIZATION_PREFIX = 'Bearer ';
