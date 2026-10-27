// OAuth 2.0 names (RFC 6749, 7636, 7009) and the identity server's paths
// (docs/auth.md). Not ours to choose, so not in contracts/.
#pragma once

namespace tarkov_companion::oauth {

inline constexpr const char* kAuthorizePath = "/authorize";
inline constexpr const char* kTokenPath = "/token";
inline constexpr const char* kRevokePath = "/revoke";
inline constexpr const char* kAccountPath = "/me";

inline constexpr const char* kParameterResponseType = "response_type";
inline constexpr const char* kParameterClientId = "client_id";
inline constexpr const char* kParameterRedirectUri = "redirect_uri";
inline constexpr const char* kParameterState = "state";
inline constexpr const char* kParameterCodeChallenge = "code_challenge";
inline constexpr const char* kParameterCodeChallengeMethod = "code_challenge_method";
inline constexpr const char* kParameterCode = "code";
inline constexpr const char* kParameterCodeVerifier = "code_verifier";
inline constexpr const char* kParameterGrantType = "grant_type";
inline constexpr const char* kParameterRefreshToken = "refresh_token";
inline constexpr const char* kParameterToken = "token";
inline constexpr const char* kParameterError = "error";

inline constexpr const char* kResponseTypeCode = "code";
inline constexpr const char* kCodeChallengeMethodS256 = "S256";
inline constexpr const char* kGrantTypeAuthorizationCode = "authorization_code";
inline constexpr const char* kGrantTypeRefreshToken = "refresh_token";
inline constexpr const char* kErrorAccessDenied = "access_denied";
inline constexpr const char* kErrorInvalidGrant = "invalid_grant";

inline constexpr const char* kTokenFieldAccessToken = "access_token";
inline constexpr const char* kTokenFieldRefreshToken = "refresh_token";
inline constexpr const char* kTokenFieldExpiresIn = "expires_in";
inline constexpr const char* kTokenFieldError = "error";

}  // namespace tarkov_companion::oauth
