#include "engine.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "json.h"
#include "test_util.h"

namespace tarkov_companion {
namespace {

constexpr const char* kGraphicsCardId = "57347ca924597744596b4e71";

TEST(Engine, ParseAndStoreReturnsSummariesInResponseOrder) {
  Engine engine({});
  std::string summaries;
  ASSERT_EQ(engine.parseAndStoreItems(test::readFixture("item_search.json"), &summaries), ParseStatus::Ok);
  auto parsed = parseJson(summaries);
  ASSERT_TRUE(parsed.ok);
  ASSERT_EQ(parsed.value.items().size(), 4u);
  EXPECT_EQ(parsed.value.items()[0]["shortName"].asString(), "Salewa");
  EXPECT_TRUE(parsed.value.items()[3]["averagePriceLast24Hours"].isNull());
  EXPECT_EQ(engine.cachedItemCount(), 4u);
}

TEST(Engine, FailedParseLeavesStateUntouched) {
  Engine engine({});
  ASSERT_EQ(engine.parseAndStoreItems(test::readFixture("item_search.json"), nullptr), ParseStatus::Ok);
  EXPECT_EQ(engine.parseAndStoreItems("{oops", nullptr), ParseStatus::InvalidJson);
  EXPECT_EQ(engine.parseAndStoreHideout(test::readFixture("api_error.json")), ParseStatus::Api);
  EXPECT_EQ(engine.cachedItemCount(), 4u);
  EXPECT_FALSE(engine.hasHideout());
}

TEST(Engine, RecommendUnknownItemIsNotFound) {
  Engine engine({});
  EXPECT_FALSE(engine.recommend("missing", {}).has_value());
}

TEST(Engine, MarksPricesAreStaleAfterTtl) {
  const std::chrono::seconds ttl(600);
  auto now = std::chrono::steady_clock::time_point{};
  Engine engine({kDefaultItemCacheCapacity, ttl}, [&] { return now; });
  ASSERT_EQ(engine.parseAndStoreItems(test::readFixture("item_search.json"), nullptr), ParseStatus::Ok);

  EXPECT_FALSE(engine.recommend(kGraphicsCardId, {})->pricesAreStale);
  now += ttl + std::chrono::seconds(1);
  EXPECT_TRUE(engine.recommend(kGraphicsCardId, {})->pricesAreStale);
}

TEST(Engine, CapacityBoundsMemory) {
  Engine engine({2, std::chrono::seconds(0)});
  ASSERT_EQ(engine.parseAndStoreItems(test::readFixture("item_search.json"), nullptr), ParseStatus::Ok);
  EXPECT_EQ(engine.cachedItemCount(), 2u);
  // Last two items of the response survive.
  EXPECT_FALSE(engine.recommend("544fb45d4bdc2dee738b4568", {}).has_value());
  EXPECT_TRUE(engine.recommend("5c093ca986f7740a1867ab12", {}).has_value());
}

TEST(Engine, ConcurrentParseAndRecommendIsSafe) {
  constexpr int kThreadCount = 8;
  constexpr int kIterationsPerThread = 200;
  Engine engine({});
  const std::string items = test::readFixture("item_search.json");
  const std::string hideout = test::readFixture("hideout.json");
  std::atomic<int> answers{0};
  std::atomic<int> writersRunning{kThreadCount / 2};

  std::vector<std::thread> threads;
  for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex) {
    const bool isWriter = threadIndex % 2 == 0;
    threads.emplace_back([&, isWriter] {
      if (isWriter) {
        for (int iteration = 0; iteration < kIterationsPerThread; ++iteration) {
          engine.parseAndStoreItems(items, nullptr);
          engine.parseAndStoreHideout(hideout);
        }
        --writersRunning;
        return;
      }

      // Readers keep going until the writers are done, then read once more:
      // under the sanitizers parsing is slow enough for a fixed number of
      // reads to finish before the first item is stored.
      bool writersWereRunning = true;
      while (writersWereRunning) {
        writersWereRunning = writersRunning.load() > 0;
        if (engine.recommend(kGraphicsCardId, {})) {
          ++answers;
        }
      }
    });
  }

  for (auto& thread : threads) thread.join();
  EXPECT_EQ(engine.cachedItemCount(), 4u);
  EXPECT_GT(answers.load(), 0);
}

}  // namespace
}  // namespace tarkov_companion
