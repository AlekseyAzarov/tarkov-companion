#include "session_config.h"

#include <gtest/gtest.h>

namespace tarkov_companion {
namespace {

TEST(SessionConfiguration, EmptyDocumentMeansDefaults) {
  auto parsed = parseSessionConfiguration("");
  ASSERT_TRUE(parsed.ok()) << parsed.error;
  EXPECT_EQ(parsed.settings.endpoint, contract::kEndpointDefault);
  EXPECT_EQ(parsed.settings.language, "en");
  EXPECT_EQ(parsed.settings.itemCacheCapacity, 512u);
  EXPECT_EQ(parsed.settings.priceTimeToLive.count(), 900);
  EXPECT_TRUE(parsed.unknownKeys.empty());
}

TEST(SessionConfiguration, ReadsEveryKey) {
  auto parsed = parseSessionConfiguration(R"({"endpoint":"https://proxy.example/gql","language":"ru",
    "userAgent":"Game/2","itemCacheCapacity":64,"priceTimeToLiveSeconds":0})");
  ASSERT_TRUE(parsed.ok()) << parsed.error;
  EXPECT_EQ(parsed.settings.endpoint, "https://proxy.example/gql");
  EXPECT_EQ(parsed.settings.language, "ru");
  EXPECT_EQ(parsed.settings.userAgent, "Game/2");
  EXPECT_EQ(parsed.settings.itemCacheCapacity, 64u);
  EXPECT_EQ(parsed.settings.priceTimeToLive.count(), 0);
}

TEST(SessionConfiguration, RejectsBadValuesNamingTheKey) {
  for (const char* bad : {
           R"({"endpoint":"http://insecure"})",
           R"({"language":"english"})",
           R"({"language":"EN"})",
           R"({"itemCacheCapacity":1})",
           R"({"itemCacheCapacity":64.5})",
           R"({"itemCacheCapacity":"64"})",
           R"({"priceTimeToLiveSeconds":-1})",
           R"([])",
           R"({oops)",
       }) {
    auto parsed = parseSessionConfiguration(bad);
    EXPECT_FALSE(parsed.ok()) << bad;
  }

  EXPECT_EQ(parseSessionConfiguration(R"({"itemCacheCapacity":1})").error, "itemCacheCapacity must be an integer in 16..10000");
}

TEST(SessionConfiguration, ReportsUnknownKeysWithoutFailing) {
  auto parsed = parseSessionConfiguration(R"({"lanugage":"de","language":"de"})");
  ASSERT_TRUE(parsed.ok());
  EXPECT_EQ(parsed.settings.language, "de");
  EXPECT_EQ(parsed.unknownKeys, std::vector<std::string>{"lanugage"});
}

TEST(PlayerProfile, FleaFollowsLevelUnlessOverridden) {
  EXPECT_FALSE(parsePlayerProfile(R"({"playerLevel":10})").settings.context.fleaUnlocked);
  EXPECT_TRUE(parsePlayerProfile(R"({"playerLevel":15})").settings.context.fleaUnlocked);
  EXPECT_TRUE(parsePlayerProfile(R"({"playerLevel":10,"fleaUnlocked":true})").settings.context.fleaUnlocked);
}

TEST(PlayerProfile, ReadsEveryKey) {
  auto parsed = parsePlayerProfile(R"({"playerLevel":42,"intelligenceCenterLevel":3,"gameMode":"pve",
    "ignoreKeepReasons":true,"fleaMarginPercent":10})");
  ASSERT_TRUE(parsed.ok()) << parsed.error;
  const PlayerContext& context = parsed.settings.context;
  EXPECT_EQ(context.playerLevel, 42);
  EXPECT_EQ(context.intelligenceCenterLevel, 3);
  EXPECT_TRUE(context.ignoreKeepReasons);
  EXPECT_DOUBLE_EQ(context.fleaMargin, 0.1);
  EXPECT_EQ(parsed.settings.gameMode, GameMode::Pve);
}

TEST(PlayerProfile, RejectsOutOfRangeValues) {
  for (const char* bad : {
           R"({"playerLevel":0})",
           R"({"intelligenceCenterLevel":4})",
           R"({"fleaMarginPercent":101})",
           R"({"gameMode":"arena"})",
           R"({"fleaUnlocked":1})",
       }) {
    EXPECT_FALSE(parsePlayerProfile(bad).ok()) << bad;
  }
}

}  // namespace
}  // namespace tarkov_companion
