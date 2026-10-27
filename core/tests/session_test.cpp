// The session against a scripted platform (fake_platform.h): an in-memory
// key-value store and an HTTP handler that answers by GraphQL operation name
// with the recorded fixtures. These are the offline-cache and orchestration rules that used to
// live in the Kotlin ItemRepository; now every platform gets them from here.
#include "session.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "fake_platform.h"
#include "json.h"
#include "test_util.h"
#include "http.h"

namespace tarkov_companion {
namespace {

using std::chrono::hours;
using std::chrono::milliseconds;
using SystemTime = std::chrono::system_clock::time_point;

constexpr const char* kSalewaId = "544fb45d4bdc2dee738b4568";
constexpr const char* kGraphicsCardId = "57347ca924597744596b4e71";
using test::FakePlatform;
using test::HttpReply;
using http::kStatusOk;
using test::Result;

class SessionTest : public ::testing::Test {
 protected:
  void SetUp() override { m_session = newSession(); }

  // A fresh session over the same store: what a process restart looks like.
  std::unique_ptr<Session> newSession(std::string_view config = "") {
    SessionClocks clocks;
    clocks.wall = [this] { return m_now; };
    std::string error;
    auto session = Session::create(config, &error, clocks);
    EXPECT_NE(session, nullptr) << error;
    // the start-up reads (favourites)
    if (session) {
      m_platform.drain(*session);
    }

    return session;
  }

  void restart() { m_session = newSession(); }

  Result search(std::string_view query) {
    RequestId request = m_nextRequest++;
    EXPECT_TRUE(m_session->search(request, query));
    m_platform.drain(*m_session);

    return resultOf(request);
  }

  Result recommend(std::string_view itemId) {
    RequestId request = m_nextRequest++;
    EXPECT_TRUE(m_session->recommend(request, itemId));
    m_platform.drain(*m_session);

    return resultOf(request);
  }

  Result resultOf(RequestId request) {
    auto found = m_platform.results.find(request);
    if (found == m_platform.results.end()) {
      ADD_FAILURE() << "no result for request " << request;
      return {};
    }

    return found->second;
  }

  void setProfile(std::string_view json) {
    std::string error;
    ASSERT_TRUE(m_session->setProfile(json, &error)) << error;
  }

  SystemTime m_now = SystemTime(hours(24 * 365 * 50));
  FakePlatform m_platform;
  std::unique_ptr<Session> m_session;
  RequestId m_nextRequest = 1;
};

TEST_F(SessionTest, SearchHitsTheNetworkAndReturnsSummariesInOrder) {
  Result result = search("  salewa ");
  ASSERT_TRUE(result.ok) << result.errorMessage;
  ASSERT_EQ(result.payload["items"].items().size(), 4u);
  EXPECT_EQ(result.payload["items"].items()[0]["id"].asString(), kSalewaId);
  EXPECT_FALSE(result.payload["fromCache"].asBool());
  EXPECT_EQ(result.payload["cacheAgeSeconds"].asInteger(), 0);

  ASSERT_EQ(m_platform.operations, std::vector<std::string>{"ItemSearch"});
  const JsonValue& variables = m_platform.variables[0];
  EXPECT_EQ(variables["name"].asString(), "salewa");
  EXPECT_EQ(variables["gameMode"].asString(), "regular");
  EXPECT_EQ(variables["lang"].asString(), "en");
  EXPECT_EQ(variables["limit"].asInteger(), Session::kSearchResultLimit);
}

TEST_F(SessionTest, OfflineSearchIsServedFromTheLastGoodResponseWithItsAge) {
  ASSERT_TRUE(search("salewa").ok);
  m_platform.offline = true;
  m_now += hours(2);

  Result result = search("SALEWA");
  ASSERT_TRUE(result.ok) << result.errorMessage;
  EXPECT_TRUE(result.payload["fromCache"].asBool());
  EXPECT_EQ(result.payload["cacheAgeSeconds"].asInteger(), 2 * 60 * 60);
  EXPECT_TRUE(m_platform.logged("warn", "airplane mode"));
}

TEST_F(SessionTest, OfflineCacheSurvivesAProcessRestart) {
  ASSERT_TRUE(search("salewa").ok);
  m_platform.offline = true;
  restart();
  EXPECT_TRUE(search("salewa").payload["fromCache"].asBool());
}

TEST_F(SessionTest, OfflineWithNothingCachedIsANetworkError) {
  m_platform.offline = true;
  Result result = search("salewa");
  EXPECT_EQ(result.errorCode, "network");
  EXPECT_NE(result.errorMessage.find("airplane mode"), std::string::npos);
}

TEST_F(SessionTest, UpstreamOutagePayloadFallsBackToCacheElseApiError) {
  m_platform.handler = [](const std::string&) { return HttpReply{http::kStatusOk, test::readFixture("api_error.json")}; };
  EXPECT_EQ(search("salewa").errorCode, "api");

  m_platform.handler = [](const std::string&) { return HttpReply{http::kStatusOk, test::readFixture("item_search.json")}; };
  ASSERT_TRUE(search("salewa").ok);
  m_platform.handler = [](const std::string&) { return HttpReply{http::kStatusOk, test::readFixture("api_error.json")}; };
  EXPECT_TRUE(search("salewa").payload["fromCache"].asBool());
}

TEST_F(SessionTest, HttpErrorsMapToApi) {
  m_platform.handler = [](const std::string&) { return HttpReply{http::kStatusTooManyRequests, ""}; };
  Result result = search("salewa");
  EXPECT_EQ(result.errorCode, "api");
  EXPECT_NE(result.errorMessage.find("429"), std::string::npos);
}

TEST_F(SessionTest, SchemaChangeIsReportedLoudlyAndDoesNotPoisonTheCache) {
  ASSERT_TRUE(search("salewa").ok);
  const std::string goodEntry = m_platform.store.begin()->second;
  m_platform.handler = [](const std::string&) { return HttpReply{http::kStatusOk, R"({"data":{"itemz":[]}})"}; };

  EXPECT_TRUE(search("salewa").payload["fromCache"].asBool()) << "falls back to the last good response";
  EXPECT_TRUE(m_platform.logged("error", "schema_mismatch"));
  EXPECT_EQ(m_platform.store.begin()->second, goodEntry);

  m_platform.store.clear();
  EXPECT_EQ(search("salewa").errorCode, "schema_mismatch");
}

TEST_F(SessionTest, InputIsValidatedBeforeAnyNetworkCall) {
  EXPECT_EQ(search(" a ").errorCode, "invalid_argument");
  EXPECT_EQ(search(std::string(65, 'x')).errorCode, "invalid_argument");
  EXPECT_EQ(recommend("../etc/passwd").errorCode, "invalid_argument");
  EXPECT_EQ(recommend("").errorCode, "invalid_argument");
  EXPECT_TRUE(m_platform.operations.empty());
  EXPECT_EQ(m_platform.cacheReads, 0);
}

TEST_F(SessionTest, QueryLengthCountsCharactersNotBytes) {
  // 40 Cyrillic letters are 80 bytes of UTF-8, but only 40 characters.
  std::string cyrillic;
  for (int letter = 0; letter < 40; ++letter) cyrillic += "а";
  EXPECT_TRUE(search(cyrillic).ok);
}

TEST_F(SessionTest, CacheKeysIncludeGameMode) {
  ASSERT_TRUE(search("salewa").ok);
  m_platform.offline = true;
  setProfile(R"({"gameMode":"pve"})");
  EXPECT_EQ(search("salewa").errorCode, "network");
}

TEST_F(SessionTest, RecommendLoadsHideoutOnceAndReusesIt) {
  ASSERT_TRUE(search("salewa").ok);
  ASSERT_TRUE(recommend(kGraphicsCardId).ok);
  ASSERT_TRUE(recommend(kSalewaId).ok);
  EXPECT_EQ(m_platform.operations, (std::vector<std::string>{"ItemSearch", "Hideout"}));
}

TEST_F(SessionTest, HideoutIsRefreshedAfterADay) {
  ASSERT_TRUE(search("salewa").ok);
  ASSERT_TRUE(recommend(kGraphicsCardId).ok);
  m_now += Session::kHideoutTimeToLive + hours(1);
  ASSERT_TRUE(recommend(kGraphicsCardId).ok);
  EXPECT_EQ(std::count(m_platform.operations.begin(), m_platform.operations.end(), "Hideout"), 2);
}

TEST_F(SessionTest, FreshHideoutFromTheCacheNeedsNoNetworkAfterRestart) {
  ASSERT_TRUE(search("salewa").ok);
  ASSERT_TRUE(recommend(kGraphicsCardId).ok);
  restart();
  ASSERT_TRUE(search("salewa").ok);
  Result result = recommend(kGraphicsCardId);
  EXPECT_FALSE(result.payload["hideoutMissing"].asBool());
  EXPECT_EQ(std::count(m_platform.operations.begin(), m_platform.operations.end(), "Hideout"), 1);
}

TEST_F(SessionTest, MissingHideoutDataDoesNotBlockAVerdict) {
  ASSERT_TRUE(search("salewa").ok);
  m_platform.handler = [](const std::string& operation) {
    if (operation == "Hideout") {
      return HttpReply{http::kStatusServiceUnavailable, ""};
    }

    return HttpReply{http::kStatusOk, test::readFixture("item_search.json")};
  };
  Result result = recommend(kGraphicsCardId);
  ASSERT_TRUE(result.ok) << result.errorMessage;
  EXPECT_TRUE(result.payload["hideoutMissing"].asBool());
  EXPECT_EQ(result.payload["recommendation"]["itemName"].asString(), "Graphics card");
}

TEST_F(SessionTest, StaleHideoutFromTheCacheBeatsNone) {
  ASSERT_TRUE(search("salewa").ok);
  ASSERT_TRUE(recommend(kGraphicsCardId).ok);
  restart();
  m_now += Session::kHideoutTimeToLive + hours(1);
  m_platform.offline = true;
  ASSERT_TRUE(search("salewa").ok);

  Result result = recommend(kGraphicsCardId);
  EXPECT_FALSE(result.payload["hideoutMissing"].asBool());
  EXPECT_EQ(result.payload["recommendation"]["verdict"].asString(), "keep");
}

TEST_F(SessionTest, ItemUnknownToTheEngineIsFetchedById) {
  // The process was restored on the item screen: nothing searched yet.
  Result result = recommend(kGraphicsCardId);
  ASSERT_TRUE(result.ok) << result.errorMessage;
  EXPECT_EQ(m_platform.operations, (std::vector<std::string>{"Hideout", "ItemById"}));
  EXPECT_FALSE(result.payload["fromCache"].asBool());
  EXPECT_EQ(m_platform.variables.back()["ids"].items()[0].asString(), kGraphicsCardId);
}

TEST_F(SessionTest, ItemThatDoesNotExistUpstreamIsNotFound) {
  m_platform.handler = [](const std::string& operation) {
    if (operation == "ItemById") {
      return HttpReply{http::kStatusOk, R"({"data":{"items":[]}})"};
    }

    return HttpReply{http::kStatusOk, test::readFixture("hideout.json")};
  };
  EXPECT_EQ(recommend("deadbeef").errorCode, "not_found");
}

TEST_F(SessionTest, VerdictsForItemsFoundOfflineAreFlagged) {
  ASSERT_TRUE(search("salewa").ok);
  m_platform.offline = true;
  restart();
  ASSERT_TRUE(search("salewa").ok);
  EXPECT_TRUE(recommend(kGraphicsCardId).payload["fromCache"].asBool());
}

TEST_F(SessionTest, PayloadCarriesTheGoldenRecommendation) {
  ASSERT_TRUE(search("salewa").ok);
  RequestId request = m_nextRequest++;
  ASSERT_TRUE(m_session->recommend(request, kSalewaId));
  // Look at the raw payload: the golden file pins the exact bytes.
  std::string payload;
  while (auto action = m_session->nextAction()) {
    if (action->resultForRequest == request) {
      payload = action->payload.value_or("");
    }

    ASSERT_TRUE(parseJson(action->json).ok);
    if (action->json.find("\"http\"") != std::string::npos) {
      JsonParseResult json = parseJson(action->json);
      m_session->completeHttp(static_cast<ActionId>(json.value["id"].asInteger()), http::kStatusOk,
                             test::readFixture("hideout.json"));
    } else if (action->json.find("\"cache_read\"") != std::string::npos) {
      JsonParseResult json = parseJson(action->json);
      m_session->completeStoredValueRead(static_cast<ActionId>(json.value["id"].asInteger()), std::nullopt);
    }
  }

  std::string golden = test::readFixture("core_recommendation.json");
  while (!golden.empty() && (golden.back() == '\n' || golden.back() == '\r')) golden.pop_back();
  EXPECT_EQ(payload, R"({"recommendation":)" + golden + R"(,"fromCache":false,"hideoutMissing":false})");
}

TEST_F(SessionTest, ProfileReachesTheVerdict) {
  constexpr const char* kHoseId = "59e35cbb86f7741778269d83";
  ASSERT_TRUE(search("hose").ok);
  // The water collector needs hoses: without this the verdict is always "keep".
  setProfile(R"({"playerLevel":30,"ignoreKeepReasons":true})");
  EXPECT_EQ(recommend(kHoseId).payload["recommendation"]["verdict"].asString(), "sell_flea");

  setProfile(R"({"playerLevel":5,"ignoreKeepReasons":true})");
  Result locked = recommend(kHoseId);
  EXPECT_EQ(locked.payload["recommendation"]["verdict"].asString(), "sell_trader");
  EXPECT_EQ(locked.payload["recommendation"]["flea"]["unavailableReason"].asString(), "flea_locked");
}

TEST_F(SessionTest, CancelSuppressesTheResultButStillFillsTheCache) {
  m_platform.holdHttp = true;
  ASSERT_TRUE(m_session->search(1, "salewa"));
  m_platform.drain(*m_session);
  m_session->cancel(1);
  m_platform.releaseHttp(*m_session);
  m_platform.drain(*m_session);

  EXPECT_TRUE(m_platform.results.empty());
  EXPECT_EQ(m_platform.store.size(), 1u) << "the response is still worth keeping";
}

TEST_F(SessionTest, CancelDropsAResultAlreadyQueued) {
  ASSERT_TRUE(m_session->search(1, "x"));  // fails validation: the result is queued immediately
  m_session->cancel(1);
  m_platform.drain(*m_session);
  EXPECT_TRUE(m_platform.results.empty());
}

TEST_F(SessionTest, CancelledRequestSkipsTheCacheFallback) {
  ASSERT_TRUE(search("salewa").ok);
  int readsBefore = m_platform.cacheReads;
  m_platform.offline = true;
  m_platform.holdHttp = true;
  ASSERT_TRUE(m_session->search(100, "salewa"));
  m_platform.drain(*m_session);
  m_session->cancel(100);
  m_platform.releaseHttp(*m_session);
  m_platform.drain(*m_session);
  EXPECT_EQ(m_platform.cacheReads, readsBefore);
}

TEST_F(SessionTest, ConcurrentRecommendsShareOneHideoutLoad) {
  ASSERT_TRUE(search("salewa").ok);
  m_platform.holdHttp = true;
  ASSERT_TRUE(m_session->recommend(10, kGraphicsCardId));
  ASSERT_TRUE(m_session->recommend(11, kSalewaId));
  m_platform.drain(*m_session);
  EXPECT_EQ(m_platform.heldHttpCount(), 1u);

  m_platform.releaseHttp(*m_session);
  m_platform.drain(*m_session);
  EXPECT_TRUE(resultOf(10).ok);
  EXPECT_TRUE(resultOf(11).ok);
}

TEST_F(SessionTest, RequestIdsCannotBeReusedWhileInFlight) {
  m_platform.holdHttp = true;
  EXPECT_TRUE(m_session->search(1, "salewa"));
  EXPECT_FALSE(m_session->search(1, "salewa"));
  EXPECT_FALSE(m_session->recommend(1, kSalewaId));
}

TEST_F(SessionTest, DataStampedInTheFutureIsServedButNotTrusted) {
  ASSERT_TRUE(search("salewa").ok);
  ASSERT_TRUE(recommend(kGraphicsCardId).ok);
  m_now -= hours(3);  // the user moved the clock back
  restart();
  m_platform.offline = true;

  Result result = search("salewa");
  EXPECT_TRUE(result.payload["fromCache"].asBool());
  EXPECT_EQ(result.payload["cacheAgeSeconds"].asInteger(), 0) << "unknown age is reported as 0, never negative";

  m_platform.offline = false;
  ASSERT_TRUE(recommend(kGraphicsCardId).ok);
  EXPECT_EQ(std::count(m_platform.operations.begin(), m_platform.operations.end(), "Hideout"), 2)
      << "hideout data from the future is refreshed";
}

TEST_F(SessionTest, CorruptCacheEntriesAreMisses) {
  ASSERT_TRUE(search("salewa").ok);
  for (auto& entry : m_platform.store) entry.second = "garbage-without-header";
  m_platform.offline = true;
  EXPECT_EQ(search("salewa").errorCode, "network");
}

TEST_F(SessionTest, UnknownSettingsKeysAreReported) {
  m_session = newSession(R"({"lanugage":"de"})");
  setProfile(R"({"level":30})");
  m_platform.drain(*m_session);
  EXPECT_TRUE(m_platform.logged("warn", "unknown config key 'lanugage'"));
  EXPECT_TRUE(m_platform.logged("warn", "unknown profile key 'level'"));
}

TEST_F(SessionTest, ConfigReachesRequests) {
  m_session = newSession(R"({"endpoint":"https://proxy.example/graphql","language":"de","userAgent":"Game/1.0"})");
  RequestId request = m_nextRequest++;
  ASSERT_TRUE(m_session->search(request, "salewa"));
  auto action = m_session->nextAction();
  ASSERT_TRUE(action);
  JsonParseResult json = parseJson(action->json);
  ASSERT_TRUE(json.ok);
  EXPECT_EQ(json.value["url"].asString(), "https://proxy.example/graphql");
  EXPECT_EQ(json.value["headers"]["User-Agent"].asString(), "Game/1.0");
  EXPECT_EQ(json.value["timeoutMilliseconds"].asInteger(), Session::kHttpTimeoutMilliseconds);
  EXPECT_EQ(parseJson(*action->payload).value["variables"]["lang"].asString(), "de");
}

TEST_F(SessionTest, CompletionsForUnknownActionsAreRejected) {
  EXPECT_FALSE(m_session->completeHttp(12345, http::kStatusOk, "{}"));
  m_platform.holdHttp = true;
  ASSERT_TRUE(m_session->search(1, "salewa"));
  auto action = m_session->nextAction();
  ASSERT_TRUE(action);
  ActionId httpAction = static_cast<ActionId>(parseJson(action->json).value["id"].asInteger());
  EXPECT_FALSE(m_session->completeStoredValueRead(httpAction, std::nullopt)) << "wrong kind of completion";
  EXPECT_TRUE(m_session->completeHttp(httpAction, http::kStatusOk, test::readFixture("item_search.json")));
  EXPECT_FALSE(m_session->completeHttp(httpAction, http::kStatusOk, "{}")) << "completed twice";
}


// Bridges inside the session. The protocol itself is covered by
// bridge_test.cpp and bridge_transcript_test.cpp; these are about the wiring.
class SessionBridgeTest : public SessionTest {
 protected:
  BridgeId open(std::string_view options = R"({"initialState":{"route":"search"}})") {
    std::string error;
    auto bridge = m_session->openBridge(options, &error);
    EXPECT_TRUE(bridge.has_value()) << error;

    return bridge.value_or(0);
  }

  void web(BridgeId bridge, const std::string& id, const std::string& type, const std::string& payload = "{}") {
    ASSERT_TRUE(m_session->bridgeReceive(bridge, R"({"version":1,"id":")" + id + R"(","type":")" + type +
                                                    R"(","payload":)" + payload + "}"));
    m_platform.drain(*m_session);
  }

  void hello(BridgeId bridge) { web(bridge, "h", "hello", R"({"protocol":1,"minimumNativeProtocol":1,"bundleVersion":"0.1.0"})"); }

  JsonValue lastSent(BridgeId bridge) { return parseJson(m_platform.sentToWeb[bridge].back()).value; }
};

TEST_F(SessionBridgeTest, SearchFromThePageUsesTheSessionsCacheAndAnalytics) {
  BridgeId bridge = open();
  hello(bridge);
  web(bridge, "w1", "search", R"({"query":"salewa"})");
  EXPECT_EQ(lastSent(bridge)["payload"]["items"].items().size(), 4u);
  EXPECT_EQ(m_platform.store.size(), 1u) << "bridge searches fill the offline cache too";
  EXPECT_EQ(m_platform.trackedOnce("search_completed").at("from_cache"), "false");
  EXPECT_TRUE(m_platform.results.empty()) << "bridge results go to the page, not to result actions";
}

TEST_F(SessionBridgeTest, ANewerSearchCancelsTheOlderOneInTheSession) {
  BridgeId bridge = open();
  hello(bridge);
  m_platform.holdHttp = true;
  web(bridge, "w1", "search", R"({"query":"sal"})");
  web(bridge, "w2", "search", R"({"query":"salewa"})");
  EXPECT_EQ(lastSent(bridge)["error"]["code"].asString(), "superseded");

  m_platform.releaseHttp(*m_session);
  m_platform.drain(*m_session);
  ASSERT_EQ(m_platform.sentToWeb[bridge].size(), 3u) << "hello, superseded, one result";
  EXPECT_EQ(lastSent(bridge)["replyTo"].asString(), "w2");
}

// The second verdict for an item is answered at once (catalog and hideout
// are warm), before the bridge's call into the session returns. It must
// still reach the page. (It didn't before the bridge registered requests
// ahead of starting them.)
TEST_F(SessionBridgeTest, AnAnswerThatComesBackAtOnceStillReachesThePage) {
  BridgeId bridge = open();
  hello(bridge);
  web(bridge, "w1", "search", R"({"query":"salewa"})");
  web(bridge, "w2", "recommend", R"({"itemId":"57347ca924597744596b4e71"})");
  size_t httpBefore = m_platform.sent.size();
  web(bridge, "w3", "recommend", R"({"itemId":"57347ca924597744596b4e71"})");
  EXPECT_EQ(m_platform.sent.size(), httpBefore) << "answered from memory";
  EXPECT_EQ(lastSent(bridge)["replyTo"].asString(), "w3");
  EXPECT_TRUE(lastSent(bridge)["ok"].asBool());
}

TEST_F(SessionBridgeTest, ClosingABridgeCancelsItsWorkAndDropsQueuedMessages) {
  BridgeId bridge = open();
  hello(bridge);
  m_platform.holdHttp = true;
  web(bridge, "w1", "search", R"({"query":"salewa"})");
  ASSERT_TRUE(m_session->bridgeReceive(bridge, R"({"version":1,"id":"w2","type":"track","payload":{"event":"x"}})"));
  size_t sentBefore = m_platform.sentToWeb[bridge].size();

  m_session->closeBridge(bridge);
  m_platform.releaseHttp(*m_session);
  m_platform.drain(*m_session);
  EXPECT_EQ(m_platform.sentToWeb[bridge].size(), sentBefore) << "neither the queued reply nor the search result";
  EXPECT_EQ(m_platform.store.size(), 1u) << "the response still lands in the offline cache";
  EXPECT_FALSE(m_session->bridgeReceive(bridge, "{}"));
  EXPECT_FALSE(m_session->bridgeEvent(bridge, "navigate_back"));
}

TEST_F(SessionBridgeTest, BridgesAreIndependent) {
  BridgeId first = open();
  BridgeId second = open(R"({"initialState":{"route":"item","itemId":"544fb45d4bdc2dee738b4568"}})");
  hello(second);
  EXPECT_TRUE(m_platform.sentToWeb[first].empty());
  EXPECT_EQ(lastSent(second)["payload"]["initialState"]["route"].asString(), "item");
  ASSERT_TRUE(m_session->bridgeEvent(second, "navigate_back"));
  ASSERT_TRUE(m_session->bridgeEvent(first, "navigate_back"));
  m_platform.drain(*m_session);
  EXPECT_EQ(lastSent(second)["event"].asString(), "navigate_back");
  EXPECT_TRUE(m_platform.sentToWeb[first].empty()) << "no events before the handshake";
}

TEST_F(SessionBridgeTest, StateAndCloseReachThePlatform) {
  BridgeId bridge = open();
  hello(bridge);
  web(bridge, "w1", "state", R"({"route":"item","itemId":"544fb45d4bdc2dee738b4568"})");
  EXPECT_EQ(m_platform.bridgeStates[bridge]["route"].asString(), "item");
  EXPECT_FALSE(m_platform.bridgeStates[bridge].hasKey("query"));
  web(bridge, "w2", "close");
  EXPECT_EQ(m_platform.closedScreens, std::vector<BridgeId>{bridge});
}

TEST_F(SessionBridgeTest, InvalidOptionsAreRejected) {
  std::string error;
  EXPECT_FALSE(m_session->openBridge(R"({"initialState":{"route":"settings"}})", &error).has_value());
  EXPECT_NE(error.find("initialState"), std::string::npos);
  EXPECT_FALSE(m_session->openBridge(R"({"initialState":{"route":"item","itemId":"../x"}})", &error).has_value());
  EXPECT_FALSE(m_session->openBridge(R"({"openedMillisecondsAgo":-1})", &error).has_value());
}

TEST_F(SessionBridgeTest, BridgeRequestIdsAreReservedForTheSession) {
  EXPECT_FALSE(m_session->search(Session::kBridgeRequestBit | 1, "salewa"));
  EXPECT_FALSE(m_session->recommend(Session::kBridgeRequestBit, kSalewaId));
}

}  // namespace
}  // namespace tarkov_companion
