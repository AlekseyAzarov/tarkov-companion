// The static catalog read when tarkov.dev's GraphQL API is down
// (docs/catalog.md): the search rules shared with scripts/build-catalog.mjs,
// and the session falling back to the catalog, then to the offline cache.
#include "catalog.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "fake_platform.h"
#include "json.h"
#include "session.h"
#include "test_util.h"
#include "http.h"

namespace tarkov_companion {
namespace {

using test::FakePlatform;
using test::HttpReply;
using http::kStatusOk;
using test::Result;

constexpr int kGraphqlUnavailable = 422;
constexpr const char* kOutage = R"({"errors":["GraphQL server unavailable. Try again later."]})";
constexpr const char* kSalewaId = "544fb45d4bdc2dee738b4568";
constexpr const char* kGraphicsCardId = "57347ca924597744596b4e71";
constexpr const char* kCatalog = "https://catalog.example/companion";

std::vector<std::string> stringsOf(const JsonValue& array) {
  std::vector<std::string> strings;
  for (const JsonValue& value : array.items()) strings.emplace_back(value.asString());

  return strings;
}

// ---------------------------------------------------------------------------
// The rules both sides implement (contracts/fixtures/catalog_search.json)

TEST(CatalogSearchTest, WordsMatchTheGenerator) {
  JsonValue cases = parseJson(test::readFixture("catalog_search.json")).value;
  for (const JsonValue& testCase : cases["words"].items()) {
    std::string text(testCase["text"].asString());
    EXPECT_EQ(catalog::lowercaseSearchWords(text), stringsOf(testCase["words"])) << text;
  }
}

TEST(CatalogSearchTest, ShardsMatchTheGenerator) {
  JsonValue cases = parseJson(test::readFixture("catalog_search.json")).value;
  for (const JsonValue& testCase : cases["shards"].items()) {
    std::string query(testCase["query"].asString());
    std::optional<std::string> shard = catalog::searchShardFileName(query);
    if (testCase["shard"].isNull()) {
      EXPECT_FALSE(shard) << query;
    } else {
      EXPECT_EQ(shard.value_or("(none)"), testCase["shard"].asString()) << query;
    }
  }
}

TEST(CatalogSearchTest, MatchingMatchesTheGenerator) {
  JsonValue cases = parseJson(test::readFixture("catalog_search.json")).value;
  for (const JsonValue& testCase : cases["matches"].items()) {
    std::string query(testCase["query"].asString());
    EXPECT_EQ(catalog::everyQueryWordStartsAWordOf(catalog::lowercaseSearchWords(query), testCase["name"].asString(),
                               testCase["shortName"].asString()),
              testCase["matches"].asBool())
        << query << " / " << testCase["name"].asString();
  }
}

TEST(CatalogSearchTest, MalformedUtf8SeparatesWords) {
  EXPECT_EQ(catalog::lowercaseSearchWords("ab\xFF" "cd\xD0"), (std::vector<std::string>{"ab", "cd"}));
}

TEST(CatalogSearchTest, AShardIsCutDownToTheMatchesInOrder) {
  std::string shard = test::readFixture("item_search.json");
  std::optional<std::string> filtered = catalog::searchResponseFromShard(shard, "salewa", 20);
  ASSERT_TRUE(filtered);
  JsonValue items = parseJson(*filtered).value["data"]["items"];
  ASSERT_EQ(items.items().size(), 1u);
  EXPECT_EQ(items.items()[0]["id"].asString(), kSalewaId);
  // Kept whole, so a verdict from it has trader offers and tasks.
  EXPECT_TRUE(items.items()[0]["sellFor"].isArray());

  // Three items have a word starting with "c"; the limit keeps the first two.
  JsonValue limited = parseJson(*catalog::searchResponseFromShard(shard, "c", 2)).value;
  ASSERT_EQ(limited["data"]["items"].items().size(), 2u);
  EXPECT_EQ(limited["data"]["items"].items()[0]["id"].asString(), kGraphicsCardId);
  EXPECT_EQ(limited["data"]["items"].items()[1]["shortName"].asString(), "Hose");
  EXPECT_FALSE(catalog::searchResponseFromShard(R"({"errors":["no"]})", "salewa", 20));
  EXPECT_FALSE(catalog::searchResponseFromShard("not json", "salewa", 20));
}

TEST(CatalogSearchTest, OnlyEnglishAndRussianAreCut) {
  EXPECT_STREQ(catalog::catalogLanguageFor("ru"), "ru");
  EXPECT_STREQ(catalog::catalogLanguageFor("en"), "en");
  EXPECT_STREQ(catalog::catalogLanguageFor("de"), "en");
}

// ---------------------------------------------------------------------------
// The session falling back to it

class CatalogSessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    m_platform.handler = [](const std::string&) { return HttpReply{kGraphqlUnavailable, kOutage}; };
    m_session = newSession(R"({"catalogUrl":"https://catalog.example/companion/"})");
  }

  std::unique_ptr<Session> newSession(std::string_view config) {
    SessionClocks clocks;
    clocks.steady = [this] { return m_steadyNow; };
    std::string error;
    auto session = Session::create(config, &error, clocks);
    EXPECT_NE(session, nullptr) << error;
    if (session) {
      m_platform.drain(*session);
    }

    return session;
  }

  // Serves `file` for `path` within the English, regular-mode catalog.
  void serve(const std::string& path, const std::string& file) {
    m_platform.getReplies[std::string(kCatalog) + "/regular/en/" + path] = HttpReply{http::kStatusOk, file};
  }

  Result search(std::string_view query) {
    RequestId request = m_nextRequest++;
    EXPECT_TRUE(m_session->search(request, query));
    m_platform.drain(*m_session);

    return m_platform.results[request];
  }

  Result recommend(std::string_view itemId) {
    RequestId request = m_nextRequest++;
    EXPECT_TRUE(m_session->recommend(request, itemId));
    m_platform.drain(*m_session);

    return m_platform.results[request];
  }

  std::chrono::steady_clock::time_point m_steadyNow{};
  FakePlatform m_platform;
  std::unique_ptr<Session> m_session;
  RequestId m_nextRequest = 1;
};

TEST_F(CatalogSessionTest, SearchFallsBackToTheShardOfTheQuery) {
  serve("search/73616c.json", test::readFixture("item_search.json"));
  Result result = search("Salewa");
  ASSERT_TRUE(result.ok) << result.errorMessage;
  ASSERT_EQ(result.payload["items"].items().size(), 1u);
  EXPECT_EQ(result.payload["items"].items()[0]["id"].asString(), kSalewaId);
  EXPECT_FALSE(result.payload["fromCache"].asBool());
  EXPECT_EQ(m_platform.operations, std::vector<std::string>{"ItemSearch"});
  EXPECT_TRUE(m_platform.logged("info", "reading the catalog for the next 10 minutes"));
  // The cache holds what the search answered, not the whole shard.
  const std::string& cached = m_platform.store.at("search|regular|en|salewa");
  EXPECT_NE(cached.find("Salewa"), std::string::npos);
  EXPECT_EQ(cached.find("Graphics card"), std::string::npos);
}

TEST_F(CatalogSessionTest, AfterAFailureGraphqlIsSkippedForAWhile) {
  serve("search/73616c.json", test::readFixture("item_search.json"));
  serve("search/677261.json", test::readFixture("item_search.json"));
  search("salewa");
  Result result = search("graphics");
  ASSERT_TRUE(result.ok) << result.errorMessage;
  EXPECT_EQ(result.payload["items"].items()[0]["id"].asString(), kGraphicsCardId);
  EXPECT_EQ(m_platform.operations.size(), 1u) << "no second GraphQL request";

  m_steadyNow += Session::kGraphqlRetryAfter;
  search("graphics");
  EXPECT_EQ(m_platform.operations.size(), 2u) << "GraphQL is tried again";
}

TEST_F(CatalogSessionTest, AWorkingGraphqlApiNeverTouchesTheCatalog) {
  m_platform.handler = [](const std::string& operation) {
    return HttpReply{http::kStatusOk, test::readFixture(operation == "Hideout" ? "hideout.json" : "item_search.json")};
  };
  EXPECT_TRUE(search("salewa").ok);
  EXPECT_TRUE(m_platform.gets.empty());
}

TEST_F(CatalogSessionTest, VerdictsReadTheItemAndHideoutFiles) {
  serve(std::string("items/") + kGraphicsCardId + ".json", test::readFixture("item_search.json"));
  serve("hideout.json", test::readFixture("hideout.json"));
  Result result = recommend(kGraphicsCardId);
  ASSERT_TRUE(result.ok) << result.errorMessage;
  EXPECT_EQ(result.payload["recommendation"]["verdict"].asString(), "keep");
  EXPECT_FALSE(result.payload["hideoutMissing"].asBool());
}

TEST_F(CatalogSessionTest, WhenBothFailTheCacheIsServed) {
  m_platform.handler = [](const std::string&) { return HttpReply{http::kStatusOk, test::readFixture("item_search.json")}; };
  ASSERT_TRUE(search("salewa").ok);

  m_platform.handler = [](const std::string&) { return HttpReply{kGraphqlUnavailable, kOutage}; };
  Result result = search("salewa");  // the catalog has no such shard: 404
  ASSERT_TRUE(result.ok) << result.errorMessage;
  EXPECT_TRUE(result.payload["fromCache"].asBool());
}

TEST_F(CatalogSessionTest, WhenEverythingFailsTheGraphqlErrorIsReported) {
  Result result = search("salewa");
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.errorCode, "api");
  EXPECT_NE(result.errorMessage.find("422"), std::string::npos) << result.errorMessage;
}

TEST_F(CatalogSessionTest, ARejectedShardIsNotCached) {
  serve("search/73616c.json", "<html>not found</html>");
  EXPECT_FALSE(search("salewa").ok);
  EXPECT_EQ(m_platform.store.count("search|regular|en|salewa"), 0u);
}

TEST_F(CatalogSessionTest, GameModeAndLanguagePickTheCatalog) {
  m_session = newSession(R"({"catalogUrl":"https://catalog.example/companion","language":"ru"})");
  std::string error;
  ASSERT_TRUE(m_session->setProfile(R"({"gameMode":"pve"})", &error)) << error;
  search("аптечка");
  ASSERT_FALSE(m_platform.gets.empty());
  EXPECT_EQ(m_platform.gets.back(), std::string(kCatalog) + "/pve/ru/search/d0b0d0bfd182.json");

  m_session = newSession(R"({"catalogUrl":"https://catalog.example/companion","language":"de"})");
  search("salewa");
  EXPECT_EQ(m_platform.gets.back(), std::string(kCatalog) + "/regular/en/search/73616c.json");
}

TEST_F(CatalogSessionTest, WithoutACatalogNothingChanges) {
  m_session = newSession("");
  EXPECT_FALSE(search("salewa").ok);
  EXPECT_TRUE(m_platform.gets.empty());
}

TEST_F(CatalogSessionTest, ACatalogUrlMustBeHttps) {
  std::string error;
  EXPECT_EQ(Session::create(R"({"catalogUrl":"http://catalog.example"})", &error), nullptr);
  EXPECT_NE(error.find("catalogUrl"), std::string::npos) << error;
}

}  // namespace
}  // namespace tarkov_companion
