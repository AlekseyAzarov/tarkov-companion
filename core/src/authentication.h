// OAuth 2.0 sign-in with PKCE in the system browser (docs/auth.md, ADR 0009).
// Runs with the session's lock held; reaches the platform only through AuthenticationHost.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "clock.h"
#include "contract_generated.h"
#include "pkce.h"
#include "session_types.h"

namespace tarkov_companion {

struct Account {
  std::string id;
  std::string displayName;
};

struct SignInConfiguration {
  std::string issuer;
  std::string clientId;
  std::string redirectUri;

  bool enabled() const { return !issuer.empty(); }
};

// Called with the session's lock held.
class AuthenticationHost {
 public:
  virtual void sendRequest(HttpRequest request, HttpContinuation then) = 0;
  // The platform's secret store: encrypted at rest, never backed up.
  virtual void readSecret(const std::string& key, StoredValueContinuation then) = 0;
  virtual void writeSecret(const std::string& key, std::string value) = 0;
  virtual void deleteSecret(const std::string& key) = 0;
  virtual void openBrowser(RequestId request, const std::string& url) = 0;
  virtual void finishWithPayload(RequestId request, std::string payloadJson) = 0;
  virtual void finishWithError(RequestId request, const Failure& failure) = 0;
  virtual void trackAnalyticsEvent(std::string event, AnalyticsProperties properties) = 0;
  virtual void log(LogLevel level, std::string message) = 0;
  virtual void notifyAccountChanged() = 0;
  virtual void storedSignInRestored() = 0;

 protected:
  ~AuthenticationHost() = default;
};

class Authentication {
 public:
  static constexpr size_t kStateEntropyBytes = 16;
  // Fresh secure-random bytes from the platform, for the verifier and the state:
  // the core has no entropy of its own.
  static constexpr size_t kSignInEntropyBytes = pkce::kVerifierEntropyBytes + kStateEntropyBytes;
  static_assert(kSignInEntropyBytes == static_cast<size_t>(contract::kSignInEntropyBytes),
                "contracts/config-limits.json shared.signInEntropyBytes is what the platform SDKs send");
  // So the access token doesn't expire while a request is in flight.
  static constexpr std::chrono::seconds kRefreshBeforeExpiry{30};
  // An "expires_in" beyond this is not believed: it would overflow the clock's
  // arithmetic, and a token is refreshed after a 401 anyway.
  static constexpr std::chrono::hours kLongestBelievedAccessTokenLifetime{24 * 365};
  static constexpr const char* kSecretStoreKey = "auth";

  using AuthorizedRequestContinuation =
      std::function<void(std::optional<Failure> failure, const HttpOutcome& response)>;

  Authentication(AuthenticationHost& host, const SignInConfiguration& configuration, WallClock wallClock);

  bool enabled() const;
  const std::optional<Account>& account() const { return m_account; }

  void restoreStoredSignIn();
  void startSignIn(RequestId request, const uint8_t* signInEntropyBytes);
  void handleRedirect(std::string_view redirectUriOrEmptyIfClosed);
  // Signs out here at once, and cancels a sign-in in progress; revoking the
  // token on the server is best effort.
  void signOut(std::optional<RequestId> request);

  // Refreshes the token first when it is about to expire, and once more after
  // a 401; waits for the stored sign-in to be restored.
  void sendWithAccessToken(HttpRequest request, AuthorizedRequestContinuation done);

 private:
  struct Tokens {
    SystemTime accessExpiresAt;
    std::string accessToken;
    std::string refreshToken;
  };
  struct PendingSignIn {
    RequestId request;
    std::string verifier;
    std::string state;
  };
  using FreshTokenContinuation = std::function<void(std::optional<Failure>)>;

  void exchangeCodeForTokens(PendingSignIn pending, std::string code);
  void fetchAccount(RequestId request, uint64_t signInAttempt);
  bool isCurrentSignInAttempt(uint64_t signInAttempt) const { return signInAttempt == m_signInAttempt; }
  bool redirectGoesToThisApp(std::string_view uri) const;
  void sendWithCurrentToken(HttpRequest request, bool alreadyRetried, AuthorizedRequestContinuation done);
  void withFreshToken(bool forceRefresh, FreshTokenContinuation then);
  void finishRefresh(const HttpOutcome& response);
  void resumeRefreshWaiters(const std::optional<Failure>& failure);
  void signOutLocally(const char* analyticsReason);
  void failSignIn(RequestId request, ErrorCode code, std::string message, const char* analyticsResult);
  std::optional<Tokens> tokensFromResponse(std::string_view tokenResponse) const;
  void persistToSecretStore();
  void setAccount(std::optional<Account> account);
  std::string issuerEndpoint(const char* path) const;
  
  WallClock m_wallClock;
  std::optional<Tokens> m_tokens;
  std::optional<PendingSignIn> m_pendingSignIn;
  // Bumped whenever the signed-in session changes, so an answer that comes
  // back for an older one (a refresh racing a sign-out) is ignored.
  uint64_t m_generation = 0;
  // Bumped by every sign-in and sign-out, so a sign-in still finishing (its
  // code being redeemed, its account fetched) can't sign the player in after
  // they signed out or started another one.
  uint64_t m_signInAttempt = 0;
  AuthenticationHost& m_host;
  const SignInConfiguration& m_configuration;
  std::optional<Account> m_account;
  std::vector<std::function<void()>> m_waitingForRestore;
  std::vector<FreshTokenContinuation> m_waitingForRefreshInFlight;
  bool m_restoring = false;
};

}  // namespace tarkov_companion
