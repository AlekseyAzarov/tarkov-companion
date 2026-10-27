// HTTP names (RFC 9110). Not ours to choose, so not in contracts/.
#pragma once

namespace tarkov_companion::http {

inline constexpr int kStatusOk = 200;
inline constexpr int kStatusBadRequest = 400;
inline constexpr int kStatusUnauthorized = 401;
inline constexpr int kStatusNotFound = 404;
inline constexpr int kStatusTooManyRequests = 429;
inline constexpr int kStatusServiceUnavailable = 503;

inline constexpr int kFirstSuccessStatus = 200;
inline constexpr int kLastSuccessStatus = 299;

inline constexpr const char* kMethodGet = "GET";
inline constexpr const char* kMethodPost = "POST";

inline constexpr const char* kHeaderAccept = "Accept";
inline constexpr const char* kHeaderAuthorization = "Authorization";
inline constexpr const char* kHeaderContentType = "Content-Type";
inline constexpr const char* kHeaderUserAgent = "User-Agent";

inline constexpr const char* kContentTypeJson = "application/json";
inline constexpr const char* kContentTypeForm = "application/x-www-form-urlencoded";

inline constexpr const char* kBearerAuthorizationPrefix = "Bearer ";

inline constexpr const char* kUrlPathSeparator = "/";
inline constexpr const char* kUrlQueryStart = "?";
inline constexpr const char* kUrlQueryValueStart = "=";

inline bool isSuccessStatus(int status) { return status >= kFirstSuccessStatus && status <= kLastSuccessStatus; }

}  // namespace tarkov_companion::http
