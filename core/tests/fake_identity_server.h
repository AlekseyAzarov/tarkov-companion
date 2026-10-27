// A scripted OAuth server for the core's tests: checks PKCE and rotates
// refresh tokens like bff/src/server.mjs. The test plays the browser
// (approve()) and plugs handle() into FakePlatform::server.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "fake_platform.h"
#include "pkce.h"
#include "http.h"

namespace tarkov_companion::test {

using std::chrono::seconds;
using SystemTime = std::chrono::system_clock::time_point;
using Sent = FakePlatform::SentRequest;

inline constexpr const char* kIssuer = "https://id.example";
inline constexpr const char* kClientId = "game-123";
inline constexpr const char* kRedirectUri = "com.example.game://tarkov-companion-auth";

inline std::map<std::string, std::string> formFields(std::string_view text) {
  std::map<std::string, std::string> fields;
  auto decode = [](std::string_view value) {
    std::string decoded;
    for (size_t index = 0; index < value.size(); ++index) {
      if (value[index] == '%' && index + 2 < value.size()) {
        decoded.push_back(static_cast<char>(std::stoi(std::string(value.substr(index + 1, 2)), nullptr, 16)));
        index += 2;
      } else {
        decoded.push_back(value[index] == '+' ? ' ' : value[index]);
      }
    }

    return decoded;
  };
  while (!text.empty()) {
    std::string_view pair = text.substr(0, text.find('&'));
    text.remove_prefix(std::min(text.size(), pair.size() + 1));
    size_t equals = pair.find('=');
    fields[decode(pair.substr(0, equals))] = equals == std::string_view::npos ? "" : decode(pair.substr(equals + 1));
  }

  return fields;
}

inline std::map<std::string, std::string> queryOf(const std::string& url) {
  size_t start = url.find('?');
  return start == std::string::npos ? std::map<std::string, std::string>{} : formFields(url.substr(start + 1));
}

class FakeIdentityServer {
 public:
  explicit FakeIdentityServer(const SystemTime& clock) : m_clock(clock) {}

  int accessLifetimeSeconds = 600;
  bool down = false;
  int tokenRequests = 0;
  int refreshRequests = 0;
  int revocations = 0;
  // /me answers 401 this many more times, as if the access token died early.
  int unauthorizedMeAnswers = 0;
  // The next refresh requests get these answers instead, in order: a busy or broken server.
  std::vector<HttpReply> refreshAnswersFirst;
  // What "expires_in" says, when it isn't the truth (accessLifetimeSeconds).
  std::optional<int64_t> statedExpiresInSeconds;
  std::vector<std::string> issuedTokens;  // every token handed out, for leak checks

  // The browser part: what the server redirects to after "Sign in".
  std::string approve(const std::string& authorizeUrl) {
    auto query = queryOf(authorizeUrl);
    EXPECT_EQ(query["response_type"], "code");
    EXPECT_EQ(query["client_id"], kClientId);
    EXPECT_EQ(query["redirect_uri"], kRedirectUri);
    EXPECT_EQ(query["code_challenge_method"], "S256");
    std::string code = "code-" + std::to_string(++m_counter);
    m_codes[code] = query["code_challenge"];

    return std::string(kRedirectUri) + "?code=" + code + "&state=" + query["state"];
  }

  std::optional<HttpReply> handle(const Sent& request) {
    if (request.url.rfind(kIssuer, 0) != 0) {
      return std::nullopt;
    }

    if (down) {
      return HttpReply{http::kStatusServiceUnavailable, ""};
    }

    std::string path = request.url.substr(std::string(kIssuer).size());
    auto form = formFields(request.body);
    if (path == "/token") {
      return token(form);
    }

    if (path == "/revoke") {
      ++revocations;
      auto grant = m_refreshTokens.find(form["token"]);
      if (grant != m_refreshTokens.end()) {
        m_revokedFamilies.insert(grant->second.family);
      }

      return HttpReply{http::kStatusOk, ""};
    }

    if (path == "/me") {
      return me(request);
    }

    return HttpReply{404, ""};
  }

 private:
  struct Grant {
    int family;
    bool used = false;
  };

  HttpReply token(std::map<std::string, std::string>& form) {
    ++tokenRequests;
    EXPECT_EQ(form["client_id"], kClientId);
    if (form["grant_type"] == "authorization_code") {
      auto code = m_codes.find(form["code"]);
      bool valid = code != m_codes.end() && pkce::s256ChallengeFor(form["code_verifier"]) == code->second &&
                   form["redirect_uri"] == kRedirectUri;
      if (code != m_codes.end()) {
        m_codes.erase(code);
      }

      return valid ? issue(++m_counter) : HttpReply{http::kStatusBadRequest, R"({"error":"invalid_grant"})"};
    }

    ++refreshRequests;
    if (!refreshAnswersFirst.empty()) {
      HttpReply answer = refreshAnswersFirst.front();
      refreshAnswersFirst.erase(refreshAnswersFirst.begin());

      return answer;
    }

    auto grant = m_refreshTokens.find(form["refresh_token"]);
    if (grant == m_refreshTokens.end() || m_revokedFamilies.count(grant->second.family)) {
      return HttpReply{http::kStatusBadRequest, R"({"error":"invalid_grant"})"};
    }

    if (grant->second.used) {
      m_revokedFamilies.insert(grant->second.family);
      return HttpReply{http::kStatusBadRequest, R"({"error":"invalid_grant"})"};
    }

    grant->second.used = true;

    return issue(grant->second.family);
  }

  HttpReply issue(int family) {
    std::string access = "access-" + std::to_string(++m_counter);
    std::string refresh = "refresh-" + std::to_string(++m_counter);
    m_accessTokens[access] = {family, m_clock + seconds(accessLifetimeSeconds)};
    m_refreshTokens[refresh] = Grant{family};
    issuedTokens.push_back(access);
    issuedTokens.push_back(refresh);

    return HttpReply{http::kStatusOk, R"({"access_token":")" + access + R"(","token_type":"Bearer","expires_in":)" +
                                        std::to_string(statedExpiresInSeconds.value_or(accessLifetimeSeconds)) +
                                        R"(,"refresh_token":")" + refresh + "\"}"};
  }

  HttpReply me(const Sent& request) {
    auto header = request.headers.find("Authorization");
    std::string bearer = header == request.headers.end() ? "" : header->second;
    auto grant = m_accessTokens.find(bearer.rfind("Bearer ", 0) == 0 ? bearer.substr(7) : "");
    bool valid = grant != m_accessTokens.end() && grant->second.second > m_clock &&
                 !m_revokedFamilies.count(grant->second.first);
    if (valid && unauthorizedMeAnswers > 0) {
      --unauthorizedMeAnswers;
      valid = false;
    }

    if (!valid) {
      return HttpReply{http::kStatusUnauthorized, ""};
    }

    return HttpReply{http::kStatusOk, R"({"id":"acc_1","displayName":"Nikita"})"};
  }

  const SystemTime& m_clock;
  int m_counter = 0;
  std::map<std::string, std::string> m_codes;  // code -> challenge
  std::map<std::string, std::pair<int, SystemTime>> m_accessTokens;
  std::map<std::string, Grant> m_refreshTokens;
  std::set<int> m_revokedFamilies;
};

}  // namespace tarkov_companion::test
