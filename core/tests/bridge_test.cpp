// The bridge state machine against a fake host whose requests stay pending
// until the test completes them.
#include "bridge.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "json.h"
#include "test_util.h"

namespace tarkov_companion {
namespace {

using std::chrono::milliseconds;

constexpr BridgeId kBridgeId = 1;
constexpr const char* kSalewaId = "544fb45d4bdc2dee738b4568";

class FakeHost : public BridgeHost {
 public:
  std::vector<JsonValue> sent;
  bool closed = false;
  std::optional<bridge::UiState> state;
  std::vector<std::pair<std::string, std::map<std::string, std::string>>> tracked;
  std::map<RequestId, std::string> searches;  // request -> query
  std::map<RequestId, std::string> recommends;
  std::vector<RequestId> cancelled;

  void sendToWeb(BridgeId bridge, std::string message) override {
    EXPECT_EQ(bridge, kBridgeId);
    JsonParseResult parsed = parseJson(message);
    EXPECT_TRUE(parsed.ok) << message;
    sent.push_back(parsed.value);
  }

  void closeScreen(BridgeId) override { closed = true; }
  void saveState(BridgeId, const bridge::UiState& newState) override { state = newState; }
  void trackAnalyticsEvent(std::string event, bridge::AnalyticsProperties properties) override {
    tracked.emplace_back(std::move(event), std::map<std::string, std::string>(properties.begin(), properties.end()));
  }

  void log(LogLevel, std::string) override {}
  RequestId newRequest(BridgeId) override { return ++m_lastRequest; }
  void startSearch(RequestId request, std::string_view query) override { searches[request] = std::string(query); }
  void startRecommend(RequestId request, std::string_view itemId) override {
    recommends[request] = std::string(itemId);
  }

  void cancelRequest(RequestId request) override { cancelled.push_back(request); }
  const RemoteFlags& remoteFlags() const override { return flags; }
  bool signInEnabled() const override { return false; }
  const std::optional<Account>& currentAccount() const override { return account; }
  void startSignInRequestedByPage(BridgeId) override {}
  void signOutRequestedByPage() override {}
  void startFavoritesList(RequestId) override {}
  void startFavoriteSet(RequestId, std::string_view, bool) override {}

  RemoteFlags flags;
  std::optional<Account> account;

  const JsonValue& last() const { return sent.back(); }

  const JsonValue& response(std::string_view id) const {
    for (auto message = sent.rbegin(); message != sent.rend(); ++message) {
      if ((*message)["replyTo"].asString() == id) {
        return *message;
      }
    }

    ADD_FAILURE() << "no response to " << id;
    static const JsonValue kNone;

    return kNone;
  }

  std::vector<std::map<std::string, std::string>> named(std::string_view event) const {
    std::vector<std::map<std::string, std::string>> matches;
    for (const auto& [name, props] : tracked) {
      if (name == event) {
        matches.push_back(props);
      }
    }

    return matches;
  }

 private:
  RequestId m_lastRequest = 0;
};

class BridgeTest : public ::testing::Test {
 protected:
  BridgeTest() { recreate(); }

  void recreate() {
    BridgeOptions bridgeOptions = m_options;
    m_bridge.reset(new Bridge(kBridgeId, std::move(bridgeOptions), m_host, [this] { return m_now; }, "0.1.0"));
  }

  void send(const std::string& id, const std::string& type, const std::string& payload = "{}") {
    m_bridge->receive(R"({"version":1,"id":")" + id + R"(","type":")" + type + R"(","payload":)" + payload + "}");
  }

  void hello(int protocol = 1, int minNative = 1, const std::string& bundle = "0.1.0") {
    send("h", "hello",
         R"({"protocol":)" + std::to_string(protocol) + R"(,"minimumNativeProtocol":)" + std::to_string(minNative) +
             R"(,"bundleVersion":")" + bundle + R"("})");
  }

  static std::string errorCode(const JsonValue& message) { return std::string(message["error"]["code"].asString()); }

  std::string recommendPayload() const {
    std::string golden = test::readFixture("core_recommendation.json");
    while (!golden.empty() && (golden.back() == '\n' || golden.back() == '\r')) golden.pop_back();

    return R"({"recommendation":)" + golden + R"(,"fromCache":false,"hideoutMissing":false})";
  }

  std::chrono::steady_clock::time_point m_now = std::chrono::steady_clock::time_point(std::chrono::seconds(10));
  BridgeOptions m_options = [] {
    BridgeOptions defaults;
    defaults.initialState = {bridge::kRouteItem, "salewa", kSalewaId};
    defaults.openedAgo = milliseconds(1000);

    return defaults;
  }();
  FakeHost m_host;
  std::unique_ptr<Bridge> m_bridge;
};

TEST_F(BridgeTest, RequestsBeforeHelloAreRefusedExceptClose) {
  send("w1", "search", R"({"query":"salewa"})");
  EXPECT_EQ(errorCode(m_host.last()), contract::kBridgeErrorCodeNotReady);
  EXPECT_TRUE(m_host.searches.empty());

  send("w2", "close");
  EXPECT_TRUE(m_host.closed);
}

TEST_F(BridgeTest, HelloReturnsCapabilitiesAndTheStateToRestore) {
  hello();
  const JsonValue& payload = m_host.response("h")["payload"];
  EXPECT_EQ(payload["protocol"].asInteger(), bridge::kNativeProtocolVersion);
  EXPECT_EQ(payload["sdkVersion"].asString(), "0.1.0");
  EXPECT_EQ(payload["iconProxy"].asString(), "/companion/icon?url=");
  EXPECT_EQ(payload["features"].items().size(), 7u) << "auth is not configured here";
  const JsonValue& state = payload["initialState"];
  EXPECT_EQ(state["route"].asString(), "item");
  EXPECT_EQ(state["itemId"].asString(), kSalewaId);
  EXPECT_EQ(m_host.named("bridge_handshake").at(0).at("result"), "ok");
}

TEST_F(BridgeTest, IncompatibleBundlesAreRefusedInBothDirections) {
  hello(1, static_cast<int>(bridge::kNativeProtocolVersion) + 1);
  EXPECT_EQ(errorCode(m_host.last()), contract::kBridgeErrorCodeIncompatibleProtocol);

  m_host.flags.minimumWebProtocol = 2;
  hello(1);
  EXPECT_EQ(errorCode(m_host.last()), contract::kBridgeErrorCodeIncompatibleProtocol);

  send("w1", "search", R"({"query":"salewa"})");
  EXPECT_EQ(errorCode(m_host.last()), contract::kBridgeErrorCodeNotReady);
}

TEST_F(BridgeTest, ABundleVersionCanBeDisabledRemotely) {
  m_host.flags.disabledBundleVersions = {"0.1.0"};
  hello();
  EXPECT_EQ(errorCode(m_host.last()), contract::kBridgeErrorCodeBundleDisabled);
  EXPECT_EQ(m_host.named("bridge_handshake").at(0).at("result"), "bundle_disabled");
}

TEST_F(BridgeTest, SearchResultsAreRelayedAsTheyAre) {
  hello();
  send("w1", "search", R"({"query":"salewa"})");
  ASSERT_EQ(m_host.searches.size(), 1u);
  m_now += milliseconds(250);
  m_bridge->onSuccess(m_host.searches.begin()->first, R"({"items":[{"id":"a"}],"fromCache":true,"cacheAgeSeconds":7200})");

  const JsonValue& payload = m_host.response("w1")["payload"];
  EXPECT_EQ(payload["items"].items().size(), 1u);
  EXPECT_TRUE(payload["fromCache"].asBool());
  EXPECT_EQ(payload["cacheAgeSeconds"].asInteger(), 7200);
  auto completed = m_host.named("search_completed").at(0);
  EXPECT_EQ(completed.at("latency_milliseconds"), "250");
  EXPECT_EQ(completed.at("from_cache"), "true");
}

TEST_F(BridgeTest, ANewerSearchSupersedesThePendingOne) {
  hello();
  send("w1", "search", R"({"query":"sal"})");
  send("w2", "search", R"({"query":"salewa"})");

  EXPECT_EQ(m_host.cancelled.size(), 1u);
  EXPECT_EQ(errorCode(m_host.response("w1")), contract::kBridgeErrorCodeSuperseded);
  // A late answer to the superseded search goes nowhere.
  size_t before = m_host.sent.size();
  m_bridge->onSuccess(m_host.cancelled.front(), R"({"items":[],"fromCache":false,"cacheAgeSeconds":0})");
  EXPECT_EQ(m_host.sent.size(), before);
}

TEST_F(BridgeTest, ErrorsCarryTheSdkErrorCode) {
  hello();
  send("w1", "recommend", R"({"itemId":"x"})");
  m_bridge->onFailure(m_host.recommends.begin()->first, Failure{ErrorCode::Network, "offline"});
  EXPECT_EQ(errorCode(m_host.response("w1")), "network");
  EXPECT_EQ(m_host.named("bridge_request_failed").at(0).at("code"), "network");
}

TEST_F(BridgeTest, FirstVerdictReportsTimeFromOverlayOpen) {
  hello();
  m_now += milliseconds(1000);  // overlay opened 1 s before the bridge, plus this second
  send("w1", "recommend", R"({"itemId":"a"})");
  send("w2", "recommend", R"({"itemId":"b"})");
  for (const auto& [request, itemId] : m_host.recommends) m_bridge->onSuccess(request, recommendPayload());

  auto verdicts = m_host.named("verdict_shown");
  ASSERT_EQ(verdicts.size(), 2u);
  EXPECT_EQ(verdicts[0].at("time_to_first_verdict_milliseconds"), "2000");
  EXPECT_EQ(verdicts[1].count("time_to_first_verdict_milliseconds"), 0u) << "only the first verdict counts";
  EXPECT_EQ(verdicts[0].at("verdict"), "keep");
  EXPECT_EQ(m_host.response("w1")["payload"]["recommendation"]["verdict"].asString(), "keep");
}

TEST_F(BridgeTest, StateUpdatesReachTheHostForProcessDeathRestore) {
  hello();
  send("w1", "state", R"({"route":"search","query":"gpu"})");
  ASSERT_TRUE(m_host.state.has_value());
  EXPECT_EQ(m_host.state->route, "search");
  EXPECT_EQ(m_host.state->query, "gpu");
  EXPECT_TRUE(m_host.state->itemId.empty());
  EXPECT_TRUE(m_host.response("w1")["ok"].asBool());
}

TEST_F(BridgeTest, WebEventsAreNamespacedInAnalytics) {
  hello();
  send("w1", "track", R"({"event":"item_opened","properties":{"source":"search"}})");
  EXPECT_EQ(m_host.named("web_item_opened").at(0).at("source"), "search");
}

TEST_F(BridgeTest, InvalidMessagesAreAnsweredAndCounted) {
  m_bridge->receive("nope");
  EXPECT_TRUE(m_host.last()["replyTo"].isNull());
  EXPECT_EQ(errorCode(m_host.last()), contract::kBridgeErrorCodeInvalidMessage);
  EXPECT_EQ(m_host.named("bridge_error").at(0).at("code"), "invalid_message");
}

TEST_F(BridgeTest, EventsWaitForTheHandshake) {
  m_bridge->sendEventOnceHandshakeIsDone("navigate_back");
  EXPECT_TRUE(m_host.sent.empty());
  hello();
  m_bridge->sendEventOnceHandshakeIsDone("navigate_back");
  EXPECT_EQ(m_host.last()["event"].asString(), "navigate_back");
}

TEST_F(BridgeTest, DisposeCancelsInFlightWorkAndSilencesLateResults) {
  hello();
  send("w1", "recommend", R"({"itemId":"a"})");
  size_t before = m_host.sent.size();

  m_bridge->cancelEverythingAndGoSilent();
  EXPECT_EQ(m_host.cancelled.size(), 1u);
  m_bridge->onFailure(m_host.recommends.begin()->first, Failure{ErrorCode::Network, "late"});
  send("w2", "search", R"({"query":"salewa"})");
  m_bridge->sendEventOnceHandshakeIsDone("navigate_back");

  EXPECT_EQ(m_host.sent.size(), before);
  EXPECT_FALSE(m_host.closed);
}

}  // namespace
}  // namespace tarkov_companion
