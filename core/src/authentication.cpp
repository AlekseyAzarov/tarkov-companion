#include "authentication.h"

#include <algorithm>
#include <utility>

#include "contract_generated.h"
#include "http.h"
#include "json.h"
#include "oauth.h"
#include "text.h"

namespace tarkov_companion {
namespace {

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::seconds;

namespace payload = contract::payload;

constexpr int kNoResponse = HttpOutcome::kNoResponseStatusCodeWithReasonInBody;
constexpr seconds kAccessTokenLifetimeWhenUnstated{3600};

constexpr const char* kStoredFieldAccessToken = "accessToken";
constexpr const char* kStoredFieldAccessExpiresAt = "accessExpiresAt";
constexpr const char* kStoredFieldRefreshToken = "refreshToken";
constexpr const char* kStoredFieldAccount = "account";

constexpr const char* kSignInResultOk = "ok";
constexpr const char* kSignInResultCancelled = "cancelled";
constexpr const char* kSignInResultFailed = "failed";
constexpr const char* kSignOutReasonUser = "user";
constexpr const char* kSignOutReasonExpired = "expired";

bool isUnreservedCharacter(char character) {
  return text::isAlphanumeric(character) || character == '-' || character == '.' || character == '_' ||
         character == '~';
}

std::string percentEncodeAllButUnreserved(std::string_view text) {
  std::string encoded;
  for (char character : text) {
    if (isUnreservedCharacter(character)) {
      encoded.push_back(character);
    } else {
      encoded.push_back('%');
      // Uppercase: RFC 3986 asks for it.
      text::appendUppercaseHex(encoded, std::string_view(&character, 1));
    }
  }

  return encoded;
}

// As servers write forms: '+' is a space.
std::string formDecode(std::string_view text) {
  std::string decoded;
  for (size_t index = 0; index < text.size(); ++index) {
    char character = text[index];
    if (character == '+') {
      decoded.push_back(' ');
      continue;
    }

    std::optional<int> highDigit = index + 2 < text.size() ? text::hexDigitValue(text[index + 1]) : std::nullopt;
    std::optional<int> lowDigit = index + 2 < text.size() ? text::hexDigitValue(text[index + 2]) : std::nullopt;
    if (character == '%' && highDigit && lowDigit) {
      decoded.push_back(static_cast<char>(*highDigit * text::kHexBase + *lowDigit));
      index += 2;
    } else {
      decoded.push_back(character);
    }
  }

  return decoded;
}

using FormFields = std::vector<std::pair<std::string, std::string>>;

std::string formEncode(const FormFields& fields) {
  std::string body;
  for (const auto& [name, value] : fields) {
    if (!body.empty()) {
      body.push_back('&');
    }
    body.append(percentEncodeAllButUnreserved(name)).push_back('=');
    body.append(percentEncodeAllButUnreserved(value));
  }

  return body;
}

FormFields queryFieldsOf(std::string_view uri) {
  FormFields fields;
  size_t queryStart = uri.find('?');
  if (queryStart == std::string_view::npos) {
    return fields;
  }

  std::string_view query = uri.substr(queryStart + 1);
  query = query.substr(0, query.find('#'));
  while (!query.empty()) {
    std::string_view pair = query.substr(0, query.find('&'));
    query.remove_prefix(std::min(query.size(), pair.size() + 1));
    size_t equals = pair.find('=');
    std::string value = equals == std::string_view::npos ? std::string() : formDecode(pair.substr(equals + 1));
    fields.emplace_back(formDecode(pair.substr(0, equals)), std::move(value));
  }

  return fields;
}

std::string fieldValueOrEmpty(const FormFields& fields, std::string_view name) {
  for (const auto& [key, value] : fields) {
    if (key == name) {
      return value;
    }
  }

  return {};
}

std::string accountJson(const Account& account) {
  JsonWriter json;
  json.beginObject()
      .key(payload::kFieldId).value(account.id)
      .key(payload::kFieldDisplayName).value(account.displayName)
      .endObject();

  return json.takeText();
}

std::string_view withoutQueryOrFragment(std::string_view uri) { return uri.substr(0, uri.find_first_of("?#")); }

std::optional<Account> accountFromJson(const JsonValue& json) {
  if (!json[payload::kFieldId].isString() || json[payload::kFieldId].asString().empty()) {
    return std::nullopt;
  }

  return Account{std::string(json[payload::kFieldId].asString()),
                 std::string(json[payload::kFieldDisplayName].asString())};
}

}  // namespace

Authentication::Authentication(AuthenticationHost& host, const SignInConfiguration& configuration,
                               WallClock wallClock)
    : m_wallClock(std::move(wallClock)), m_host(host), m_configuration(configuration) {}

bool Authentication::enabled() const { return m_configuration.enabled(); }

std::string Authentication::issuerEndpoint(const char* path) const { return m_configuration.issuer + path; }

// ---------------------------------------------------------------------------
// The stored sign-in

void Authentication::restoreStoredSignIn() {
  m_restoring = true;
  m_host.readSecret(kSecretStoreKey, [this](std::optional<std::string_view> storedValue) {
    m_restoring = false;

    if (storedValue) {
      JsonParseResult stored = parseJson(*storedValue);
      std::optional<Account> account = stored.ok ? accountFromJson(stored.value[kStoredFieldAccount]) : std::nullopt;
      std::string refreshToken(stored.value[kStoredFieldRefreshToken].asString());
      if (account && !refreshToken.empty()) {
        m_tokens = Tokens{SystemTime(milliseconds(stored.value[kStoredFieldAccessExpiresAt].asInteger())),
                          std::string(stored.value[kStoredFieldAccessToken].asString()), refreshToken};
        setAccount(std::move(account));
      } else {
        m_host.log(LogLevel::Warn, "ignoring an unreadable stored sign-in");
        m_host.deleteSecret(kSecretStoreKey);
      }
    }

    auto waiting = std::move(m_waitingForRestore);
    m_waitingForRestore.clear();
    for (auto& resume : waiting) {
      resume();
    }
    m_host.storedSignInRestored();
  });
}

void Authentication::persistToSecretStore() {
  if (!m_tokens || !m_account) {
    return;
  }

  JsonWriter json;
  json.beginObject()
      .key(kStoredFieldAccessToken).value(m_tokens->accessToken)
      .key(kStoredFieldAccessExpiresAt).value(millisecondsSinceEpoch(m_tokens->accessExpiresAt))
      .key(kStoredFieldRefreshToken).value(m_tokens->refreshToken)
      .key(kStoredFieldAccount).rawJson(accountJson(*m_account))
      .endObject();
  m_host.writeSecret(kSecretStoreKey, json.takeText());
}

void Authentication::setAccount(std::optional<Account> account) {
  m_account = std::move(account);
  m_host.notifyAccountChanged();
}

// ---------------------------------------------------------------------------
// Signing in

void Authentication::startSignIn(RequestId request, const uint8_t* signInEntropyBytes) {
  if (!enabled()) {
    m_host.finishWithError(request, {ErrorCode::InvalidArgument, "sign-in is not configured"});
    return;
  }

  if (m_pendingSignIn) {
    RequestId replaced = m_pendingSignIn->request;
    m_pendingSignIn.reset();
    failSignIn(replaced, ErrorCode::SignInCancelled, "replaced by a newer sign-in", kSignInResultCancelled);
  }
  // A previous sign-in still redeeming its code is replaced too.
  ++m_signInAttempt;

  const uint8_t* stateEntropyBytes = signInEntropyBytes + pkce::kVerifierEntropyBytes;
  m_pendingSignIn = PendingSignIn{request, pkce::verifierFromRandomBytes(signInEntropyBytes),
                                  pkce::base64UrlWithoutPadding(stateEntropyBytes, kStateEntropyBytes)};

  std::string query = formEncode({{oauth::kParameterResponseType, oauth::kResponseTypeCode},
                                  {oauth::kParameterClientId, m_configuration.clientId},
                                  {oauth::kParameterRedirectUri, m_configuration.redirectUri},
                                  {oauth::kParameterState, m_pendingSignIn->state},
                                  {oauth::kParameterCodeChallenge, pkce::s256ChallengeFor(m_pendingSignIn->verifier)},
                                  {oauth::kParameterCodeChallengeMethod, oauth::kCodeChallengeMethodS256}});
  std::string url = issuerEndpoint(oauth::kAuthorizePath) + http::kUrlQueryStart + query;
  m_host.openBrowser(request, url);
}

// The scheme is case-insensitive (RFC 3986); the host and the path must be the registered ones.
bool Authentication::redirectGoesToThisApp(std::string_view uri) const {
  std::string_view target = withoutQueryOrFragment(uri);
  std::string_view expected = m_configuration.redirectUri;
  size_t schemeEnd = expected.find(':');
  if (target.size() != expected.size() || schemeEnd == std::string_view::npos) {
    return false;
  }

  bool sameScheme =
      text::asciiLowercase(target.substr(0, schemeEnd)) == text::asciiLowercase(expected.substr(0, schemeEnd));

  return sameScheme && target.substr(schemeEnd) == expected.substr(schemeEnd);
}

void Authentication::handleRedirect(std::string_view redirectUriOrEmptyIfClosed) {
  if (!m_pendingSignIn) {
    m_host.log(LogLevel::Warn, "sign-in redirect with no sign-in in progress; ignored");
    return;
  }

  std::string_view uri = redirectUriOrEmptyIfClosed;
  if (uri.empty()) {
    RequestId request = m_pendingSignIn->request;
    m_pendingSignIn.reset();
    failSignIn(request, ErrorCode::SignInCancelled, "the sign-in page was closed", kSignInResultCancelled);
    return;
  }

  FormFields fields = queryFieldsOf(uri);
  // The state ties this answer to this sign-in: anything else is a stale
  // page or a forged link. Its code must not be redeemed, and it must not
  // cancel the real sign-in either, whose answer may still come.
  bool answersThisSignIn =
      redirectGoesToThisApp(uri) && fieldValueOrEmpty(fields, oauth::kParameterState) == m_pendingSignIn->state;
  if (!answersThisSignIn) {
    m_host.log(LogLevel::Warn, "ignoring a sign-in redirect that does not answer the sign-in in progress");
    return;
  }

  PendingSignIn pending = std::move(*m_pendingSignIn);
  m_pendingSignIn.reset();
  std::string error = fieldValueOrEmpty(fields, oauth::kParameterError);
  if (error == oauth::kErrorAccessDenied) {
    failSignIn(pending.request, ErrorCode::SignInCancelled, "sign-in declined", kSignInResultCancelled);
    return;
  }

  std::string code = fieldValueOrEmpty(fields, oauth::kParameterCode);
  if (!error.empty() || code.empty()) {
    failSignIn(pending.request, ErrorCode::SignInFailed, "identity server: " + (error.empty() ? "no code" : error),
               kSignInResultFailed);
    return;
  }

  exchangeCodeForTokens(std::move(pending), std::move(code));
}

void Authentication::exchangeCodeForTokens(PendingSignIn pending, std::string code) {
  std::string body = formEncode({{oauth::kParameterGrantType, oauth::kGrantTypeAuthorizationCode},
                                 {oauth::kParameterCode, code},
                                 {oauth::kParameterRedirectUri, m_configuration.redirectUri},
                                 {oauth::kParameterClientId, m_configuration.clientId},
                                 {oauth::kParameterCodeVerifier, pending.verifier}});
  RequestId request = pending.request;
  uint64_t signInAttempt = m_signInAttempt;
  HttpRequest tokenRequest{http::kMethodPost, issuerEndpoint(oauth::kTokenPath),
                           {{http::kHeaderContentType, http::kContentTypeForm}}, std::move(body)};

  m_host.sendRequest(std::move(tokenRequest), [this, request, signInAttempt](const HttpOutcome& response) {
    if (!isCurrentSignInAttempt(signInAttempt)) {
      failSignIn(request, ErrorCode::SignInCancelled, "signed out or replaced before the sign-in finished",
                 kSignInResultCancelled);
      return;
    }
    if (response.statusCode == kNoResponse) {
      failSignIn(request, ErrorCode::Network, "network unavailable: " + std::string(response.body),
                 kSignInResultFailed);
      return;
    }

    std::optional<Tokens> tokens =
        response.statusCode == http::kStatusOk ? tokensFromResponse(response.body) : std::nullopt;
    if (!tokens || tokens->refreshToken.empty()) {
      failSignIn(request, ErrorCode::SignInFailed,
                 "token request refused (HTTP " + std::to_string(response.statusCode) + ")", kSignInResultFailed);
      return;
    }

    ++m_generation;
    m_tokens = std::move(tokens);
    fetchAccount(request, signInAttempt);
  });
}

void Authentication::fetchAccount(RequestId request, uint64_t signInAttempt) {
  HttpRequest accountRequest{http::kMethodGet, issuerEndpoint(oauth::kAccountPath), {}, std::nullopt};
  uint64_t generationOfNewTokens = m_generation;
  sendWithAccessToken(std::move(accountRequest), [this, request, signInAttempt, generationOfNewTokens](
                                                     std::optional<Failure> failure, const HttpOutcome& response) {
    if (!isCurrentSignInAttempt(signInAttempt)) {
      // Tokens nobody has signed in with yet are dropped; a sign-out already did that.
      if (!m_account && generationOfNewTokens == m_generation) {
        ++m_generation;
        m_tokens.reset();
      }
      failSignIn(request, ErrorCode::SignInCancelled, "signed out or replaced before the sign-in finished",
                 kSignInResultCancelled);
      return;
    }

    std::optional<Account> account;
    if (!failure && response.statusCode == http::kStatusOk) {
      JsonParseResult json = parseJson(response.body);
      if (json.ok) {
        account = accountFromJson(json.value);
      }
    }

    if (!account) {
      ++m_generation;
      m_tokens.reset();
      failSignIn(request, failure ? failure->code : ErrorCode::SignInFailed,
                 failure ? failure->message : "the identity server returned no account", kSignInResultFailed);
      return;
    }

    setAccount(std::move(account));
    persistToSecretStore();
    m_host.trackAnalyticsEvent(contract::kEventSignInCompleted, {{contract::kPropertyResult, kSignInResultOk}});
    JsonWriter result;
    result.beginObject().key(payload::kFieldAccount).rawJson(accountJson(*m_account)).endObject();
    m_host.finishWithPayload(request, result.takeText());
  });
}

void Authentication::failSignIn(RequestId request, ErrorCode code, std::string message, const char* analyticsResult) {
  // The message never holds a token or a code: only what went wrong.
  LogLevel level = code == ErrorCode::SignInCancelled ? LogLevel::Info : LogLevel::Warn;
  m_host.log(level, "sign-in " + std::string(analyticsResult) + ": " + message);
  m_host.trackAnalyticsEvent(contract::kEventSignInCompleted, {{contract::kPropertyResult, analyticsResult}});
  m_host.finishWithError(request, {code, std::move(message)});
}

std::optional<Authentication::Tokens> Authentication::tokensFromResponse(std::string_view tokenResponse) const {
  JsonParseResult json = parseJson(tokenResponse);
  const JsonValue& accessToken = json.value[oauth::kTokenFieldAccessToken];
  if (!json.ok || !accessToken.isString() || accessToken.asString().empty()) {
    return std::nullopt;
  }

  int64_t statedLifetimeSeconds =
      json.value[oauth::kTokenFieldExpiresIn].asInteger(kAccessTokenLifetimeWhenUnstated.count());
  // Negative reads as already expired: the token is refreshed before its first use.
  int64_t longestBelievedSeconds = duration_cast<seconds>(kLongestBelievedAccessTokenLifetime).count();
  seconds lifetime(std::clamp<int64_t>(statedLifetimeSeconds, 0, longestBelievedSeconds));

  return Tokens{m_wallClock() + lifetime, std::string(accessToken.asString()),
                std::string(json.value[oauth::kTokenFieldRefreshToken].asString())};
}

// ---------------------------------------------------------------------------
// Signing out

void Authentication::signOut(std::optional<RequestId> request) {
  ++m_signInAttempt;
  if (m_pendingSignIn) {
    RequestId abandoned = m_pendingSignIn->request;
    m_pendingSignIn.reset();
    failSignIn(abandoned, ErrorCode::SignInCancelled, "signed out", kSignInResultCancelled);
  }

  if (m_tokens) {
    // Best effort: the player is signed out here whatever the server says,
    // and with no network the refresh token simply expires there.
    std::string body = formEncode(
        {{oauth::kParameterToken, m_tokens->refreshToken},
         {oauth::kParameterClientId, m_configuration.clientId}});
    HttpRequest revokeRequest{http::kMethodPost, issuerEndpoint(oauth::kRevokePath),
                              {{http::kHeaderContentType, http::kContentTypeForm}},
                              std::move(body)};
    m_host.sendRequest(std::move(revokeRequest), [this](const HttpOutcome& response) {
      if (response.statusCode != http::kStatusOk) {
        m_host.log(LogLevel::Warn, "token revocation failed (HTTP " + std::to_string(response.statusCode) + ")");
      }
    });
    signOutLocally(kSignOutReasonUser);
  }

  if (request) {
    m_host.finishWithPayload(*request, kEmptyJsonObject);
  }
}

void Authentication::signOutLocally(const char* analyticsReason) {
  ++m_generation;
  m_tokens.reset();
  m_host.deleteSecret(kSecretStoreKey);
  setAccount(std::nullopt);
  m_host.trackAnalyticsEvent(contract::kEventSignedOut, {{contract::kPropertyReason, analyticsReason}});
}

// ---------------------------------------------------------------------------
// Requests with the access token, and refresh

void Authentication::sendWithAccessToken(HttpRequest request, AuthorizedRequestContinuation done) {
  if (m_restoring) {
    m_waitingForRestore.push_back(
        [this, request, done]() mutable { sendWithAccessToken(std::move(request), std::move(done)); });
    return;
  }
  if (!m_tokens) {
    done(Failure{ErrorCode::NotSignedIn, "not signed in"}, HttpOutcome{});
    return;
  }

  withFreshToken(false, [this, request, done](std::optional<Failure> failure) mutable {
    if (failure) {
      done(failure, HttpOutcome{});
      return;
    }

    sendWithCurrentToken(std::move(request), false, std::move(done));
  });
}

void Authentication::sendWithCurrentToken(HttpRequest request, bool alreadyRetried,
                                          AuthorizedRequestContinuation done) {
  HttpRequest withToken = request;
  withToken.headersBesidesAcceptAndUserAgent.emplace_back(http::kHeaderAuthorization,
                                                          http::kBearerAuthorizationPrefix + m_tokens->accessToken);
  uint64_t generation = m_generation;

  m_host.sendRequest(std::move(withToken), [this, request, done, generation,
                                               alreadyRetried](const HttpOutcome& response) {
    if (response.statusCode == kNoResponse) {
      done(Failure{ErrorCode::Network, "network unavailable: " + std::string(response.body)}, response);
      return;
    }
    if (response.statusCode != http::kStatusUnauthorized) {
      done(std::nullopt, response);
      return;
    }

    // The token died early (revoked, server restarted): refresh once and try
    // again. A second 401 means the session is gone.
    bool sameSignedInSession = generation == m_generation && m_tokens;
    if (alreadyRetried || !sameSignedInSession) {
      if (sameSignedInSession) {
        signOutLocally(kSignOutReasonExpired);
      }
      done(Failure{ErrorCode::NotSignedIn, "the session expired"}, response);
      return;
    }

    withFreshToken(true, [this, request, done](std::optional<Failure> failure) mutable {
      if (failure) {
        done(failure, HttpOutcome{});
        return;
      }

      sendWithCurrentToken(std::move(request), true, std::move(done));
    });
  });
}

// One refresh at a time: whoever asks while one is in flight waits for it.
void Authentication::withFreshToken(bool forceRefresh, FreshTokenContinuation then) {
  if (!m_tokens) {
    then(Failure{ErrorCode::NotSignedIn, "not signed in"});
    return;
  }

  bool tokenStillFresh = m_wallClock() + kRefreshBeforeExpiry < m_tokens->accessExpiresAt;
  if (!forceRefresh && m_waitingForRefreshInFlight.empty() && tokenStillFresh) {
    then(std::nullopt);
    return;
  }

  m_waitingForRefreshInFlight.push_back(std::move(then));
  if (m_waitingForRefreshInFlight.size() > 1) {
    return;
  }

  std::string body = formEncode({{oauth::kParameterGrantType, oauth::kGrantTypeRefreshToken},
                                 {oauth::kParameterRefreshToken, m_tokens->refreshToken},
                                 {oauth::kParameterClientId, m_configuration.clientId}});
  HttpRequest refreshRequest{http::kMethodPost, issuerEndpoint(oauth::kTokenPath),
                             {{http::kHeaderContentType, http::kContentTypeForm}}, std::move(body)};
  uint64_t generation = m_generation;

  m_host.sendRequest(std::move(refreshRequest), [this, generation](const HttpOutcome& response) {
    if (generation != m_generation || !m_tokens) {
      // Signed out (or in again) meanwhile: this answer is for a session that is gone.
      resumeRefreshWaiters(Failure{ErrorCode::NotSignedIn, "not signed in"});
      return;
    }

    finishRefresh(response);
  });
}

// Only an answer that says the refresh token is no good signs the player out:
// invalid_grant (expired, revoked, or a reused token made the server revoke
// its whole family), or 401. A rate limit, a timeout or an outage of the
// identity server keeps the session, and the next request tries again.
void Authentication::finishRefresh(const HttpOutcome& response) {
  if (response.statusCode == kNoResponse) {
    resumeRefreshWaiters(Failure{ErrorCode::Network, "network unavailable: " + std::string(response.body)});
    return;
  }

  bool refreshTokenRejected = response.statusCode == http::kStatusUnauthorized;
  if (response.statusCode == http::kStatusBadRequest) {
    JsonParseResult errorResponse = parseJson(response.body);
    refreshTokenRejected =
        errorResponse.ok && errorResponse.value[oauth::kTokenFieldError].asString() == oauth::kErrorInvalidGrant;
  }
  if (refreshTokenRejected) {
    m_host.log(LogLevel::Warn, "signed out: the session expired or was revoked");
    signOutLocally(kSignOutReasonExpired);
    resumeRefreshWaiters(Failure{ErrorCode::NotSignedIn, "the session expired"});
    return;
  }

  std::optional<Tokens> refreshed =
      response.statusCode == http::kStatusOk ? tokensFromResponse(response.body) : std::nullopt;
  if (!refreshed) {
    std::string status = std::to_string(response.statusCode);
    resumeRefreshWaiters(Failure{ErrorCode::Api, "token refresh failed (HTTP " + status + "); still signed in"});
    return;
  }

  // Refresh tokens rotate; a server that doesn't rotate leaves it out.
  if (refreshed->refreshToken.empty()) {
    refreshed->refreshToken = m_tokens->refreshToken;
  }
  m_tokens = std::move(refreshed);
  persistToSecretStore();
  resumeRefreshWaiters(std::nullopt);
}

void Authentication::resumeRefreshWaiters(const std::optional<Failure>& failure) {
  auto waiting = std::move(m_waitingForRefreshInFlight);
  m_waitingForRefreshInFlight.clear();
  for (auto& resume : waiting) {
    resume(failure);
  }
}

}  // namespace tarkov_companion
