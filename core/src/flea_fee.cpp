#include "flea_fee.h"

#include <cmath>

namespace tarkov_companion {
namespace {

// The constants of the formula documented in flea_fee.h.
constexpr double kBaseValueTaxRate = 0.03;
constexpr double kAskingPriceTaxRate = 0.03;
constexpr double kFeeExponentBase = 4.0;
constexpr double kOverpricingPenaltyPower = 1.08;

constexpr int kIntelligenceCenterDiscountLevel = 3;
constexpr double kIntelligenceCenterFeeMultiplier = 0.7;

}  // namespace

int64_t fleaListingFee(int64_t basePrice, int64_t listPrice, int intelligenceCenterLevel) {
  if (basePrice <= 0 || listPrice <= 0) {
    return 0;
  }

  const double baseValue = static_cast<double>(basePrice);
  const double askingPrice = static_cast<double>(listPrice);

  double baseValueExponent = std::log10(baseValue / askingPrice);
  double askingPriceExponent = std::log10(askingPrice / baseValue);
  if (askingPrice < baseValue) {
    baseValueExponent = std::pow(baseValueExponent, kOverpricingPenaltyPower);
  } else {
    askingPriceExponent = std::pow(askingPriceExponent, kOverpricingPenaltyPower);
  }

  double fee = baseValue * kBaseValueTaxRate * std::pow(kFeeExponentBase, baseValueExponent) +
               askingPrice * kAskingPriceTaxRate * std::pow(kFeeExponentBase, askingPriceExponent);
  if (intelligenceCenterLevel >= kIntelligenceCenterDiscountLevel) {
    fee *= kIntelligenceCenterFeeMultiplier;
  }

  return static_cast<int64_t>(std::llround(fee));
}

}  // namespace tarkov_companion
