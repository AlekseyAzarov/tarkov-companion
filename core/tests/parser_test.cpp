#include "tarkov_parser.h"

#include <gtest/gtest.h>

#include "test_util.h"

namespace tarkov_companion {
namespace {

TEST(ItemsParser, ParsesRecordedFixture) {
  auto parsed = parseItemsResponse(test::readFixture("item_search.json"));
  ASSERT_EQ(parsed.status, ParseStatus::Ok);
  ASSERT_EQ(parsed.items.size(), 4u);

  const Item& salewa = parsed.items[0];
  EXPECT_EQ(salewa.id, "544fb45d4bdc2dee738b4568");
  EXPECT_EQ(salewa.nameAndIcon.shortName, "Salewa");
  EXPECT_EQ(salewa.basePrice, 21000);
  EXPECT_EQ(salewa.lastLowPrice, 25000);
  EXPECT_EQ(salewa.inventorySlots(), 2);
  EXPECT_EQ(salewa.sellFor.size(), 3u);
  ASSERT_EQ(salewa.usedInTasks.size(), 1u);
  EXPECT_EQ(salewa.usedInTasks[0].name, "Shortage");

  const Item& kappa = parsed.items[3];
  EXPECT_TRUE(kappa.bannedFromFlea);
  EXPECT_FALSE(kappa.averagePriceLast24Hours.has_value());
  EXPECT_FALSE(kappa.lastLowPrice.has_value());
}

TEST(ItemsParser, ToleratesUnknownFieldsAndSkipsIncompleteItems) {
  auto parsed = parseItemsResponse(R"({"data":{"items":[
    {"id":"a","name":"A","newUpstreamField":{"x":1},"sellFor":[{"priceRUB":0,"vendor":{"normalizedName":"x"}}]},
    {"name":"no id"},
    {"id":"b","name":"B","width":null}
  ]}})");
  ASSERT_EQ(parsed.status, ParseStatus::Ok);
  ASSERT_EQ(parsed.items.size(), 2u);
  EXPECT_EQ(parsed.skippedWithoutIdOrName, 1);
  EXPECT_TRUE(parsed.items[0].sellFor.empty()) << "zero-priced offers are dropped";
  EXPECT_EQ(parsed.items[1].width, 1) << "null dimensions fall back to 1";
}

TEST(ItemsParser, ClassifiesFailures) {
  EXPECT_EQ(parseItemsResponse("not json").status, ParseStatus::InvalidJson);
  EXPECT_EQ(parseItemsResponse(test::readFixture("api_error.json")).status, ParseStatus::Api);
  EXPECT_EQ(parseItemsResponse(R"({"data":{"items":{}}})").status, ParseStatus::Schema);
  EXPECT_EQ(parseItemsResponse(R"([1,2])").status, ParseStatus::Schema);
}

TEST(ItemsParser, AcceptsPartialDataWithErrors) {
  auto parsed = parseItemsResponse(
      R"({"errors":[{"message":"one field failed"}],"data":{"items":[{"id":"a","name":"A"}]}})");
  EXPECT_EQ(parsed.status, ParseStatus::Ok);
  EXPECT_EQ(parsed.items.size(), 1u);
}

TEST(HideoutParser, IndexesRequirementsByItem) {
  auto parsed = parseHideoutResponse(test::readFixture("hideout.json"));
  ASSERT_EQ(parsed.status, ParseStatus::Ok);

  auto graphicsCard = parsed.requirementsByItemId.find("57347ca924597744596b4e71");
  ASSERT_NE(graphicsCard, parsed.requirementsByItemId.end());
  ASSERT_EQ(graphicsCard->second.size(), 1u);
  EXPECT_EQ(graphicsCard->second[0].stationName, "Bitcoin farm");
  EXPECT_EQ(graphicsCard->second[0].level, 2);

  EXPECT_EQ(parsed.requirementsByItemId.count("ignored-zero-count"), 0u);
}

}  // namespace
}  // namespace tarkov_companion
