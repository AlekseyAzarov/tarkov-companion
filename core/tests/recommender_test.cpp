#include "recommender.h"

#include <gtest/gtest.h>

#include "json.h"
#include "tarkov_parser.h"
#include "test_util.h"

namespace tarkov_companion {
namespace {

class RecommenderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto items = parseItemsResponse(test::readFixture("item_search.json"));
    ASSERT_EQ(items.status, ParseStatus::Ok);
    for (auto& item : items.items) m_itemsByShortName[item.nameAndIcon.shortName] = item;
    auto hideout = parseHideoutResponse(test::readFixture("hideout.json"));
    ASSERT_EQ(hideout.status, ParseStatus::Ok);
    m_hideout = hideout.requirementsByItemId;
  }

  const Item& item(const std::string& shortName) { return m_itemsByShortName.at(shortName); }

  std::unordered_map<std::string, Item> m_itemsByShortName;
  HideoutRequirementsByItemId m_hideout;
  PlayerContext m_context;
};

TEST_F(RecommenderTest, QuestItemIsKeptButStillPriced) {
  auto recommendation = recommend(item("Salewa"), &m_hideout, m_context);
  EXPECT_EQ(recommendation.verdict, Verdict::Keep);
  ASSERT_EQ(recommendation.keepReasons.size(), 1u);
  EXPECT_EQ(recommendation.keepReasons[0].kind, KeepReason::Kind::Task);
  EXPECT_EQ(recommendation.keepReasons[0].taskOrStationLabel, "Shortage");
  EXPECT_GT(recommendation.bestNetRubles, 0) << "a kept item still reports what selling would give";
}

TEST_F(RecommenderTest, IgnoringKeepReasonsFallsThroughToBestSale) {
  m_context.ignoreKeepReasons = true;
  auto recommendation = recommend(item("Salewa"), &m_hideout, m_context);
  EXPECT_EQ(recommendation.verdict, Verdict::SellFlea);
  ASSERT_TRUE(recommendation.flea);
  EXPECT_EQ(recommendation.flea->listPrice, 24999) << "undercuts the lowest offer by 1";
  EXPECT_EQ(recommendation.bestNetRubles, recommendation.flea->net);
  EXPECT_EQ(recommendation.bestNetPerSlot, recommendation.bestNetRubles / 2);
}

TEST_F(RecommenderTest, HideoutRequirementTriggersKeep) {
  auto recommendation = recommend(item("GPU"), &m_hideout, m_context);
  EXPECT_EQ(recommendation.verdict, Verdict::Keep);
  ASSERT_EQ(recommendation.keepReasons.size(), 1u);
  EXPECT_EQ(recommendation.keepReasons[0].taskOrStationLabel, "Bitcoin farm L2");
}

TEST_F(RecommenderTest, WithoutHideoutIndexOnlyTasksCount) {
  auto recommendation = recommend(item("GPU"), nullptr, m_context);
  EXPECT_EQ(recommendation.verdict, Verdict::SellFlea);
}

TEST_F(RecommenderTest, LockedFleaFallsBackToTraderButKeepsNumbers) {
  m_context.fleaUnlocked = false;
  auto recommendation = recommend(item("Hose"), nullptr, m_context);
  EXPECT_EQ(recommendation.verdict, Verdict::SellTrader);
  ASSERT_TRUE(recommendation.bestTrader);
  EXPECT_EQ(recommendation.bestTrader->vendorId, "mechanic");
  EXPECT_EQ(recommendation.bestNetRubles, 9450);
  ASSERT_TRUE(recommendation.flea);
  EXPECT_FALSE(recommendation.flea->available);
  EXPECT_EQ(recommendation.flea->unavailableReason, "flea_locked");
  EXPECT_GT(recommendation.flea->net, 0);
}

TEST_F(RecommenderTest, FleaMustBeatTradersByMargin) {
  Item hose = item("Hose");
  hose.sellFor = {{"mechanic", "Mechanic", 15500}};
  // Flea nets ~16 000: more than 15 500, but not by 5%.
  EXPECT_EQ(recommend(hose, nullptr, m_context).verdict, Verdict::SellTrader);

  m_context.fleaMargin = 0.0;
  EXPECT_EQ(recommend(hose, nullptr, m_context).verdict, Verdict::SellFlea);
}

TEST_F(RecommenderTest, NoFleaAndNoTradersMeansNoBuyer) {
  auto recommendation = recommend(item("Kappa"), nullptr, m_context);
  EXPECT_EQ(recommendation.verdict, Verdict::NoBuyer);
  EXPECT_EQ(recommendation.bestNetRubles, 0);
  ASSERT_TRUE(recommendation.flea);
  EXPECT_EQ(recommendation.flea->unavailableReason, "no_flea");
}

TEST_F(RecommenderTest, SerializesToStableJson) {
  auto json = recommendationToJson(recommend(item("Salewa"), &m_hideout, m_context));
  auto parsed = parseJson(json);
  ASSERT_TRUE(parsed.ok) << json;
  const JsonValue& root = parsed.value;
  EXPECT_EQ(root["verdict"].asString(), "keep");
  EXPECT_EQ(root["itemId"].asString(), "544fb45d4bdc2dee738b4568");
  EXPECT_EQ(root["bestTrader"]["vendorId"].asString(), "therapist");
  EXPECT_TRUE(root["flea"]["available"].asBool());
  EXPECT_EQ(root["keepReasons"].items()[0]["kind"].asString(), "task");
}

// contracts/fixtures/core_recommendation.json is the documented output shape.
// The web UI's validators are tested against the same file
// (web-ui/test/protocol.test.ts), so a change here that the UI can't parse
// fails one of the two suites.
TEST_F(RecommenderTest, MatchesGoldenContract) {
  std::string golden = test::readFixture("core_recommendation.json");
  while (!golden.empty() && (golden.back() == '\n' || golden.back() == '\r')) golden.pop_back();
  EXPECT_EQ(recommendationToJson(recommend(item("Salewa"), &m_hideout, m_context)), golden);
}

}  // namespace
}  // namespace tarkov_companion
