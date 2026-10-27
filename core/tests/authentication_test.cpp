// Sign-in against a scripted identity server (fake_identity_server.h) that
// checks PKCE and rotates refresh tokens like bff/src/server.mjs, with the
// "browser" played by the test.
#include "authentication.h"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "fake_identity_server.h"
#include "fake_platform.h"
#include "json.h"
#include "pkce.h"
#include "session.h"
#include "http.h"

namespace tarkov_companion {

// Reaches the session's Authentication for flows the C API only exercises in stage 4b
// (authorized requests). Single-threaded tests: the lock is not needed.
class SessionTestAccess {
 public:
  static Authentication& authentication(Session& session) { return session.m_authentication; }
};

namespace {

using std::chrono::seconds;
using test::Sent;
using test::SystemTime;

using test::FakeIdentityServer;
using test::formFields;
using test::kClientId;
using test::kIssuer;
using test::kRedirectUri;
using test::queryOf;

class AuthTest : public ::testing::Test {
 protected:
  void SetUp() override {
    m_platform.server = [this](const Sent& request) { return m_server.handle(request); };
    m_session = newSession();
  }

  std::unique_ptr<Session> newSession(std::string config = "") {
    if (config.empty()) {
      config = std::string(R"({"signInIssuer":")") + kIssuer + R"(","signInClientId":")" + kClientId +
               R"(","signInRedirectUri":")" + kRedirectUri + R"("})";
    }

    SessionClocks clocks;
    clocks.wall = [this] { return m_now; };
    std::string error;
    auto session = Session::create(config, &error, clocks);
    EXPECT_NE(session, nullptr) << error;
    if (session) {
      m_platform.drain(*session);
    }

    return session;
  }

  // Fresh "random" bytes for each sign-in.
  std::vector<uint8_t> entropy() {
    std::vector<uint8_t> bytes(Authentication::kSignInEntropyBytes);
    for (auto& byte : bytes) byte = static_cast<uint8_t>(++m_entropyCounter);

    return bytes;
  }

  RequestId startSignIn() {
    RequestId request = m_nextRequest++;
    auto bytes = entropy();
    EXPECT_TRUE(m_session->startSignIn(request, bytes.data(), bytes.size()));
    m_platform.drain(*m_session);

    return request;
  }

  test::Result signIn() {
    RequestId request = startSignIn();
    m_session->signInRedirect(m_server.approve(m_platform.openedUrls.back()));
    m_platform.drain(*m_session);

    return m_platform.results[request];
  }

  test::Result authorizedMe() {
    test::Result outcome;
    SessionTestAccess::authentication(*m_session).sendWithAccessToken(
        HttpRequest{"GET", std::string(kIssuer) + "/me", {}, std::nullopt},
        [&outcome](std::optional<Failure> failure, const HttpOutcome& response) {
          outcome.ok = !failure && response.statusCode == http::kStatusOk;
          if (failure) {
            outcome.errorCode = errorCodeName(failure->code);
          }
        });
    m_platform.drain(*m_session);

    return outcome;
  }

  bool signedIn() const { return !m_platform.accounts.empty() && m_platform.accounts.back()["signedIn"].asBool(); }

  SystemTime m_now = SystemTime(std::chrono::hours(24 * 365 * 50));
  FakeIdentityServer m_server{m_now};
  test::FakePlatform m_platform;
  std::unique_ptr<Session> m_session;
  RequestId m_nextRequest = 1;
  int m_entropyCounter = 0;
};

TEST_F(AuthTest, SignInOpensTheSystemBrowserWithPkce) {
  auto bytes = entropy();
  ASSERT_TRUE(m_session->startSignIn(1, bytes.data(), bytes.size()));
  m_platform.drain(*m_session);
  ASSERT_EQ(m_platform.openedUrls.size(), 1u);
  auto query = queryOf(m_platform.openedUrls[0]);
  EXPECT_EQ(m_platform.openedUrls[0].rfind(std::string(kIssuer) + "/authorize?", 0), 0u);
  EXPECT_EQ(query["code_challenge"], pkce::s256ChallengeFor(pkce::verifierFromRandomBytes(bytes.data())));
  EXPECT_EQ(query["state"], pkce::base64UrlWithoutPadding(bytes.data() + pkce::kVerifierEntropyBytes, Authentication::kStateEntropyBytes));
  EXPECT_TRUE(m_platform.sent.empty()) << "nothing goes to the server before the player signs in";
}

TEST_F(AuthTest, TooLittleEntropyIsRefused) {
  uint8_t bytes[Authentication::kSignInEntropyBytes - 1] = {};
  EXPECT_FALSE(m_session->startSignIn(1, bytes, sizeof(bytes)));
}

TEST_F(AuthTest, FullSignInStoresTheSessionAndReportsTheAccount) {
  test::Result result = signIn();
  ASSERT_TRUE(result.ok) << result.errorMessage;
  EXPECT_EQ(result.payload["account"]["displayName"].asString(), "Nikita");
  EXPECT_TRUE(signedIn());
  EXPECT_EQ(m_platform.accounts.back()["id"].asString(), "acc_1");
  EXPECT_EQ(m_platform.secrets.count(Authentication::kSecretStoreKey), 1u);
  EXPECT_EQ(m_platform.trackedOnce("sign_in_completed").at("result"), "ok");

  const Sent& exchange = m_platform.sent.front();
  EXPECT_EQ(exchange.headers.at("Content-Type"), "application/x-www-form-urlencoded");
  EXPECT_EQ(formFields(exchange.body)["grant_type"], "authorization_code");
}

TEST_F(AuthTest, TheSignInSurvivesARestartWithoutNetwork) {
  ASSERT_TRUE(signIn().ok);
  m_server.down = true;
  m_platform.accounts.clear();
  m_session = newSession();
  ASSERT_TRUE(signedIn());
  EXPECT_EQ(m_platform.accounts.size(), 1u) << "one notification for one restore";
  EXPECT_EQ(m_platform.accounts.back()["displayName"].asString(), "Nikita");
}

TEST_F(AuthTest, AForgedRedirectIsNotRedeemedAndDoesNotCancelTheRealSignIn) {
  RequestId request = startSignIn();
  std::string redirect = m_server.approve(m_platform.openedUrls.back());
  std::string forged = redirect.substr(0, redirect.find("state=") + 6) + "forged";
  m_session->signInRedirect(forged);
  m_platform.drain(*m_session);
  EXPECT_EQ(m_platform.results.count(request), 0u) << "any app can send that link; the sign-in goes on";
  EXPECT_EQ(m_server.tokenRequests, 0);
  EXPECT_TRUE(m_platform.logged("warn", "does not answer the sign-in in progress"));

  m_session->signInRedirect(redirect);
  m_platform.drain(*m_session);
  EXPECT_TRUE(m_platform.results[request].ok);
}

TEST_F(AuthTest, OnlyARedirectToThisAppsUriIsRedeemed) {
  RequestId request = startSignIn();
  std::string redirect = m_server.approve(m_platform.openedUrls.back());
  std::string query = redirect.substr(redirect.find('?'));
  // The right state, but a lookalike target: a prefix match would take it.
  m_session->signInRedirect(std::string(kRedirectUri) + ".evil.example" + query);
  m_session->signInRedirect(std::string(kRedirectUri) + "/elsewhere" + query);
  m_platform.drain(*m_session);
  EXPECT_EQ(m_server.tokenRequests, 0);

  // Schemes are case-insensitive: a browser may hand it back either way.
  m_session->signInRedirect("COM.EXAMPLE.GAME://tarkov-companion-auth" + query);
  m_platform.drain(*m_session);
  EXPECT_TRUE(m_platform.results[request].ok);
}

TEST_F(AuthTest, ClosingOrDecliningTheSignInIsACancellation) {
  RequestId closed = startSignIn();
  m_session->signInRedirect("");
  m_platform.drain(*m_session);
  EXPECT_EQ(m_platform.results[closed].errorCode, "sign_in_cancelled");

  RequestId declined = startSignIn();
  std::string state = queryOf(m_platform.openedUrls.back())["state"];
  m_session->signInRedirect(std::string(kRedirectUri) + "?error=access_denied&state=" + state);
  m_platform.drain(*m_session);
  EXPECT_EQ(m_platform.results[declined].errorCode, "sign_in_cancelled");
  EXPECT_EQ(m_platform.tracked.size(), 2u);
}

TEST_F(AuthTest, ANewSignInReplacesOneInProgress) {
  RequestId first = startSignIn();
  RequestId second = startSignIn();
  EXPECT_EQ(m_platform.results[first].errorCode, "sign_in_cancelled");
  m_session->signInRedirect(m_server.approve(m_platform.openedUrls.back()));
  m_platform.drain(*m_session);
  EXPECT_TRUE(m_platform.results[second].ok);
}

TEST_F(AuthTest, ARedirectWithNoSignInInProgressIsIgnored) {
  m_session->signInRedirect(std::string(kRedirectUri) + "?code=x&state=y");
  m_platform.drain(*m_session);
  EXPECT_TRUE(m_platform.logged("warn", "no sign-in in progress"));
  EXPECT_TRUE(m_platform.sent.empty());
}

TEST_F(AuthTest, AnExpiredAccessTokenIsRefreshedFirst) {
  ASSERT_TRUE(signIn().ok);
  m_now += seconds(m_server.accessLifetimeSeconds);
  EXPECT_TRUE(authorizedMe().ok);
  EXPECT_EQ(m_server.refreshRequests, 1);
}

TEST_F(AuthTest, ConcurrentRequestsShareOneRefresh) {
  ASSERT_TRUE(signIn().ok);
  m_now += seconds(m_server.accessLifetimeSeconds);
  m_platform.holdHttp = true;
  int answered = 0;
  for (int index = 0; index < 3; ++index) {
    SessionTestAccess::authentication(*m_session).sendWithAccessToken(
        HttpRequest{"GET", std::string(kIssuer) + "/me", {}, std::nullopt},
        [&answered](std::optional<Failure> failure, const HttpOutcome& response) {
          if (!failure && response.statusCode == http::kStatusOk) {
            ++answered;
          }
        });
  }

  m_platform.drain(*m_session);
  EXPECT_EQ(m_platform.heldHttpCount(), 1u) << "one refresh, the requests wait for it";
  m_platform.holdHttp = false;
  m_platform.releaseHttp(*m_session);
  m_platform.drain(*m_session);
  EXPECT_EQ(answered, 3);
  EXPECT_EQ(m_server.refreshRequests, 1);
}

TEST_F(AuthTest, A401IsRetriedOnceAfterARefresh) {
  ASSERT_TRUE(signIn().ok);
  m_server.unauthorizedMeAnswers = 1;
  EXPECT_TRUE(authorizedMe().ok);
  EXPECT_EQ(m_server.refreshRequests, 1);

  m_server.unauthorizedMeAnswers = 2;
  EXPECT_EQ(authorizedMe().errorCode, "not_signed_in");
  EXPECT_FALSE(signedIn()) << "two 401s in a row: the session is gone";
}

TEST_F(AuthTest, AReusedRefreshTokenSignsOut) {
  ASSERT_TRUE(signIn().ok);
  JsonValue stolen = parseJson(m_platform.secrets[Authentication::kSecretStoreKey]).value;
  m_now += seconds(m_server.accessLifetimeSeconds);
  ASSERT_TRUE(authorizedMe().ok);  // rotates the refresh token

  // The thief replays the old refresh token: the server revokes the family.
  Sent replay{0, "POST", std::string(kIssuer) + "/token", {},
              "grant_type=refresh_token&client_id=game-123&refresh_token=" +
                  std::string(stolen["refreshToken"].asString())};
  m_server.handle(replay);

  m_now += seconds(m_server.accessLifetimeSeconds);
  EXPECT_EQ(authorizedMe().errorCode, "not_signed_in");
  EXPECT_FALSE(signedIn());
  EXPECT_EQ(m_platform.secrets.count(Authentication::kSecretStoreKey), 0u);
  EXPECT_EQ(m_platform.trackedOnce("signed_out").at("reason"), "expired");
}

TEST_F(AuthTest, ARateLimitedOrBrokenRefreshKeepsTheSession) {
  ASSERT_TRUE(signIn().ok);
  m_server.refreshAnswersFirst = {{http::kStatusTooManyRequests, ""},
                                  {http::kStatusBadRequest, R"({"error":"invalid_request"})"},
                                  {http::kStatusServiceUnavailable, ""}};
  m_now += seconds(m_server.accessLifetimeSeconds);
  for (int attempt = 0; attempt < 3; ++attempt) {
    EXPECT_EQ(authorizedMe().errorCode, "api") << "attempt " << attempt;
    EXPECT_TRUE(signedIn()) << "attempt " << attempt;
  }

  EXPECT_TRUE(authorizedMe().ok) << "the server is back; the refresh token still works";
  EXPECT_EQ(m_platform.secrets.count(Authentication::kSecretStoreKey), 1u);
}

TEST_F(AuthTest, NoNetworkDuringRefreshKeepsTheSession) {
  ASSERT_TRUE(signIn().ok);
  m_now += seconds(m_server.accessLifetimeSeconds);
  m_platform.offline = true;
  EXPECT_EQ(authorizedMe().errorCode, "network");
  EXPECT_TRUE(signedIn());
  m_platform.offline = false;
  EXPECT_TRUE(authorizedMe().ok);
}

TEST_F(AuthTest, SignOutIsImmediateAndRevokesBestEffort) {
  ASSERT_TRUE(signIn().ok);
  m_platform.offline = true;
  ASSERT_TRUE(m_session->signOut(50));
  m_platform.drain(*m_session);
  EXPECT_TRUE(m_platform.results[50].ok);
  EXPECT_FALSE(signedIn());
  EXPECT_EQ(m_platform.secrets.count(Authentication::kSecretStoreKey), 0u);
  EXPECT_TRUE(m_platform.logged("warn", "token revocation failed"));
  EXPECT_EQ(m_platform.trackedOnce("signed_out").at("reason"), "user");
  EXPECT_EQ(authorizedMe().errorCode, "not_signed_in");
}

TEST_F(AuthTest, SigningOutWhileTheCodeIsRedeemedLeavesThePlayerSignedOut) {
  RequestId request = startSignIn();
  m_platform.holdHttp = true;
  m_session->signInRedirect(m_server.approve(m_platform.openedUrls.back()));
  m_platform.drain(*m_session);
  ASSERT_EQ(m_platform.heldHttpCount(), 1u) << "the code exchange is in flight";

  ASSERT_TRUE(m_session->signOut(60));
  m_platform.holdHttp = false;
  m_platform.releaseHttp(*m_session);
  m_platform.drain(*m_session);

  EXPECT_EQ(m_platform.results[request].errorCode, "sign_in_cancelled");
  EXPECT_FALSE(signedIn());
  EXPECT_EQ(m_platform.secrets.count(Authentication::kSecretStoreKey), 0u);
  EXPECT_EQ(authorizedMe().errorCode, "not_signed_in");
}

TEST_F(AuthTest, SigningOutWhileTheAccountIsFetchedLeavesThePlayerSignedOut) {
  RequestId request = startSignIn();
  m_platform.holdHttp = true;
  m_session->signInRedirect(m_server.approve(m_platform.openedUrls.back()));
  m_platform.drain(*m_session);
  m_platform.releaseHttp(*m_session);  // tokens arrive; the account request is held
  m_platform.drain(*m_session);
  ASSERT_EQ(m_server.tokenRequests, 1);

  ASSERT_TRUE(m_session->signOut(60));
  m_platform.holdHttp = false;
  m_platform.releaseHttp(*m_session);
  m_platform.drain(*m_session);

  EXPECT_EQ(m_platform.results[request].errorCode, "sign_in_cancelled");
  EXPECT_FALSE(signedIn());
  EXPECT_EQ(m_platform.secrets.count(Authentication::kSecretStoreKey), 0u);
  EXPECT_EQ(m_server.revocations, 1) << "the tokens just issued are revoked";
}

TEST_F(AuthTest, SigningOutCancelsASignInWaitingForTheBrowser) {
  RequestId request = startSignIn();
  std::string redirect = m_server.approve(m_platform.openedUrls.back());
  ASSERT_TRUE(m_session->signOut(60));
  m_platform.drain(*m_session);
  EXPECT_EQ(m_platform.results[request].errorCode, "sign_in_cancelled");

  m_session->signInRedirect(redirect);
  m_platform.drain(*m_session);
  EXPECT_EQ(m_server.tokenRequests, 0);
  EXPECT_FALSE(signedIn());
}

TEST_F(AuthTest, AnUnbelievableTokenLifetimeIsCapped) {
  // Seconds enough to overflow the clock's arithmetic if believed.
  m_server.statedExpiresInSeconds = 1'000'000'000'000'000;
  ASSERT_TRUE(signIn().ok);
  m_now += seconds(m_server.accessLifetimeSeconds);
  EXPECT_TRUE(authorizedMe().ok) << "the token died on the server: a 401, then a refresh";
  EXPECT_EQ(m_server.refreshRequests, 1);
}

TEST_F(AuthTest, ANegativeTokenLifetimeMeansRefreshFirst) {
  m_server.statedExpiresInSeconds = -5;
  ASSERT_TRUE(signIn().ok);
  EXPECT_EQ(m_server.refreshRequests, 1) << "fetching the account already needed a fresh token";
  EXPECT_TRUE(signedIn());
}

TEST_F(AuthTest, TokensNeverReachLogsAnalyticsOrThePage) {
  std::string error;
  auto bridge = m_session->openBridge("", &error);
  ASSERT_TRUE(bridge.has_value()) << error;
  ASSERT_TRUE(m_session->bridgeReceive(
      *bridge, R"({"version":1,"id":"h","type":"hello","payload":{"protocol":1,"minimumNativeProtocol":1,"bundleVersion":"0.1.0"}})"));
  m_platform.drain(*m_session);
  ASSERT_TRUE(signIn().ok);
  m_now += seconds(m_server.accessLifetimeSeconds);
  m_server.unauthorizedMeAnswers = 2;
  authorizedMe();  // refresh, retry, forced sign-out: the noisy path

  std::string visible;
  for (const auto& [level, message] : m_platform.logs) visible += message + "\n";
  for (const auto& [event, props] : m_platform.tracked) {
    for (const auto& prop : props) visible += prop.second + "\n";
  }

  for (const auto& message : m_platform.sentToWeb[*bridge]) visible += message + "\n";
  ASSERT_FALSE(m_server.issuedTokens.empty());
  for (const auto& token : m_server.issuedTokens) EXPECT_EQ(visible.find(token), std::string::npos) << token;
}

TEST_F(AuthTest, ThePageSeesTheAccountButNotTheTokens) {
  std::string error;
  auto bridge = m_session->openBridge("", &error);
  ASSERT_TRUE(bridge.has_value()) << error;
  auto web = [&](const std::string& message) {
    EXPECT_TRUE(m_session->bridgeReceive(*bridge, message));
    m_platform.drain(*m_session);

    return parseJson(m_platform.sentToWeb[*bridge].back()).value;
  };
  JsonValue hello = web(R"({"version":1,"id":"h","type":"hello","payload":{"protocol":1,"minimumNativeProtocol":1,"bundleVersion":"0.1.0"}})");
  EXPECT_FALSE(hello["payload"]["account"]["signedIn"].asBool());
  const auto& features = hello["payload"]["features"].items();
  EXPECT_TRUE(std::any_of(features.begin(), features.end(), [](const JsonValue& f) { return f.asString() == contract::bridge::kFeatureSignIn; }));

  EXPECT_TRUE(web(R"({"version":1,"id":"s","type":"sign_in","payload":{}})")["ok"].asBool());
  EXPECT_EQ(m_platform.signInsFromPages, std::vector<BridgeId>{*bridge});

  // The last message of a kind: signing in or out also refreshes the favourites.
  auto lastWhere = [&](const std::string& field, const std::string& value) {
    const auto& sent = m_platform.sentToWeb[*bridge];
    for (auto message = sent.rbegin(); message != sent.rend(); ++message) {
      JsonValue json = parseJson(*message).value;
      if (json[field].asString() == value) {
        return json;
      }
    }

    ADD_FAILURE() << "no message with " << field << " = " << value;

    return JsonValue();
  };

  ASSERT_TRUE(signIn().ok);
  JsonValue changed = lastWhere("event", "account_changed");
  EXPECT_EQ(changed["payload"]["account"]["displayName"].asString(), "Nikita");
  EXPECT_FALSE(changed["payload"]["account"].hasKey("id"));

  web(R"({"version":1,"id":"o","type":"sign_out","payload":{}})");
  EXPECT_FALSE(signedIn());
  EXPECT_TRUE(lastWhere("replyTo", "o")["ok"].asBool());
  EXPECT_FALSE(lastWhere("event", "account_changed")["payload"]["account"]["signedIn"].asBool());
}

TEST_F(AuthTest, WithoutAuthConfigThePageCannotSignIn) {
  m_session = newSession("{}");
  std::string error;
  auto bridge = m_session->openBridge("", &error);
  ASSERT_TRUE(bridge.has_value());
  ASSERT_TRUE(m_session->bridgeReceive(
      *bridge, R"({"version":1,"id":"h","type":"hello","payload":{"protocol":1,"minimumNativeProtocol":1,"bundleVersion":"0.1.0"}})"));
  ASSERT_TRUE(m_session->bridgeReceive(*bridge, R"({"version":1,"id":"s","type":"sign_in","payload":{}})"));
  m_platform.drain(*m_session);
  JsonValue hello = parseJson(m_platform.sentToWeb[*bridge].front()).value;
  EXPECT_FALSE(hello["payload"].hasKey("account"));
  EXPECT_EQ(parseJson(m_platform.sentToWeb[*bridge].back()).value["error"]["code"].asString(), "unsupported_type");
}

TEST_F(AuthTest, HttpIsAllowedOnlyToThisMachineAndOnlyInDevelopmentMode) {
  auto accepted = [](const std::string& config) {
    std::string error;

    return Session::create(config, &error) != nullptr;
  };
  std::string local = R"("signInIssuer":"http://10.0.2.2:8787","signInClientId":"c","signInRedirectUri":"app://x")";
  EXPECT_FALSE(accepted("{" + local + "}"));
  EXPECT_TRUE(accepted("{" + local + R"(,"developmentMode":true})"));
  EXPECT_FALSE(accepted(R"({"developmentMode":true,"signInIssuer":"http://id.example","signInClientId":"c","signInRedirectUri":"app://x"})"));
  EXPECT_FALSE(accepted(R"({"developmentMode":true,"signInIssuer":"http://10.0.2.2.evil.example","signInClientId":"c","signInRedirectUri":"app://x"})"));
  EXPECT_FALSE(accepted(R"({"signInIssuer":"https://id.example"})")) << "needs a client id and a redirect";
  std::string issuerAndClient = R"("signInIssuer":"https://id.example","signInClientId":"c")";
  EXPECT_TRUE(accepted("{" + issuerAndClient + R"(,"signInRedirectUri":"com.studio.game://x"})"));
  EXPECT_FALSE(accepted("{" + issuerAndClient + R"(,"signInRedirectUri":"com.Studio.Game://x"})"))
      << "Android matches schemes case-sensitively, browsers lowercase them";
  EXPECT_FALSE(accepted("{" + issuerAndClient + R"(,"signInRedirectUri":"com.studio.my_game://x"})"))
      << "'_' is not allowed in a scheme";
}

}  // namespace
}  // namespace tarkov_companion
