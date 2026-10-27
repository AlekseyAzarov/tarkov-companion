#include "lru_cache.h"

#include <gtest/gtest.h>

#include <string>

namespace tarkov_companion {
namespace {

constexpr std::chrono::seconds kNoExpiry{0};

struct FakeClock {
  std::chrono::steady_clock::time_point now{};
  SteadyClock asClock() {
    return [this] { return now; };
  }
};

TEST(LruCache, EvictsLeastRecentlyUsed) {
  LruCache<std::string, int> cache(2, kNoExpiry);
  cache.put("a", 1);
  cache.put("b", 2);
  ASSERT_NE(cache.get("a").value, nullptr);  // touch "a": "b" is now the oldest
  cache.put("c", 3);

  EXPECT_TRUE(cache.contains("a"));
  EXPECT_FALSE(cache.contains("b"));
  EXPECT_TRUE(cache.contains("c"));
  EXPECT_EQ(cache.evictionCount(), 1u);
}

TEST(LruCache, PutOverwritesWithoutGrowing) {
  LruCache<std::string, int> cache(2, kNoExpiry);
  cache.put("a", 1);
  cache.put("a", 2);
  EXPECT_EQ(cache.size(), 1u);
  EXPECT_EQ(*cache.get("a").value, 2);
}

TEST(LruCache, ExpiredEntriesAreFlaggedNotDropped) {
  const std::chrono::seconds ttl(60);
  FakeClock clock;
  LruCache<std::string, int> cache(4, ttl, clock.asClock());
  cache.put("a", 1);

  clock.now += ttl - std::chrono::seconds(1);
  EXPECT_FALSE(cache.get("a").expired);

  clock.now += std::chrono::seconds(2);
  auto lookup = cache.get("a");
  ASSERT_NE(lookup.value, nullptr);
  EXPECT_TRUE(lookup.expired);

  cache.put("a", 2);  // refresh resets the age
  EXPECT_FALSE(cache.get("a").expired);
}

TEST(LruCache, ZeroCapacityIsClampedToOne) {
  LruCache<int, int> cache(0, kNoExpiry);
  cache.put(1, 1);
  cache.put(2, 2);
  EXPECT_EQ(cache.size(), 1u);
  EXPECT_TRUE(cache.contains(2));
}

}  // namespace
}  // namespace tarkov_companion
