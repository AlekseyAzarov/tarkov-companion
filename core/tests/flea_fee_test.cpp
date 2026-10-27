#include "flea_fee.h"

#include <gtest/gtest.h>

namespace tarkov_companion {
namespace {

constexpr int kNoIntelligenceCenter = 0;
constexpr int kIntelligenceCenterMaximumLevel = 3;

TEST(FleaFee, ListingAtBasePriceCostsSixPercent) {
  // Both exponents are 0, so fee = base * 0.03 + price * 0.03.
  EXPECT_EQ(fleaListingFee(10000, 10000, kNoIntelligenceCenter), 600);
}

TEST(FleaFee, OverpricingIsPenalisedSuperlinearly) {
  // Hand-computed: 300 * 4^log10(0.5) + 600 * 4^(log10(2)^1.08) = 197.6 + 876.6
  EXPECT_NEAR(fleaListingFee(10000, 20000, kNoIntelligenceCenter), 1074, 1);
}

TEST(FleaFee, IntelligenceCenterThreeDiscountsThirtyPercent) {
  EXPECT_EQ(fleaListingFee(10000, 10000, kIntelligenceCenterMaximumLevel), 420);
  EXPECT_EQ(fleaListingFee(10000, 10000, kIntelligenceCenterMaximumLevel - 1), 600);
}

TEST(FleaFee, OverpricingOnlyEverCostsMore) {
  const int64_t basePrice = 20000;
  const int64_t step = 1000;
  int64_t previousFee = fleaListingFee(basePrice, basePrice, kNoIntelligenceCenter);
  for (int64_t listPrice = basePrice + step; listPrice <= 10 * basePrice; listPrice += step) {
    int64_t fee = fleaListingFee(basePrice, listPrice, kNoIntelligenceCenter);
    EXPECT_GE(fee, previousFee) << "list price " << listPrice;
    previousFee = fee;
  }
}

// The fee is not monotonic in the list price. Slightly under base it dips (the
// asking-price term shrinks), but dumping far below base is punished by the
// base-value term: at base/10 the fee is 2400 + 15 vs 1200 at base.
TEST(FleaFee, DumpingFarBelowBaseIsPunished) {
  const int64_t basePrice = 20000;
  EXPECT_LT(fleaListingFee(basePrice, basePrice * 9 / 10, kNoIntelligenceCenter), fleaListingFee(basePrice, basePrice, kNoIntelligenceCenter));
  EXPECT_NEAR(fleaListingFee(basePrice, basePrice / 10, kNoIntelligenceCenter), 2415, 1);
}

TEST(FleaFee, NetProceedsGrowWithListPriceAboveBase) {
  const int64_t basePrice = 20000;
  const int64_t step = 1000;
  int64_t previousNet = basePrice - fleaListingFee(basePrice, basePrice, kNoIntelligenceCenter);
  for (int64_t listPrice = basePrice + step; listPrice <= 10 * basePrice; listPrice += step) {
    int64_t net = listPrice - fleaListingFee(basePrice, listPrice, kNoIntelligenceCenter);
    EXPECT_GT(net, previousNet) << "list price " << listPrice;
    previousNet = net;
  }
}

TEST(FleaFee, InvalidInputsCostNothing) {
  EXPECT_EQ(fleaListingFee(0, 1000, kNoIntelligenceCenter), 0);
  EXPECT_EQ(fleaListingFee(1000, 0, kNoIntelligenceCenter), 0);
  EXPECT_EQ(fleaListingFee(-5, 1000, kNoIntelligenceCenter), 0);
}

}  // namespace
}  // namespace tarkov_companion
