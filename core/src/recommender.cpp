#include "recommender.h"

#include <algorithm>

#include "contract_generated.h"
#include "flea_fee.h"
#include "item_name_and_icon_json.h"
#include "json.h"

namespace tarkov_companion {

namespace payload = contract::payload;

const char* verdictName(Verdict verdict) {
  switch (verdict) {
    case Verdict::Keep: return payload::kVerdictKeep;
    case Verdict::SellFlea: return payload::kVerdictSellFlea;
    case Verdict::SellTrader: return payload::kVerdictSellTrader;
    case Verdict::NoBuyer: return payload::kVerdictNoBuyer;
  }

  return payload::kVerdictNoBuyer;
}

namespace {

// Undercut the current lowest flea offer by this much so the listing sells.
constexpr int64_t kFleaUndercutRubles = 1;
constexpr int64_t kMinimumListPriceRubles = 1;

std::optional<TraderOption> bestTraderOffer(const Item& item) {
  std::optional<TraderOption> best;
  for (const auto& offer : item.sellFor) {
    if (offer.vendorId == kFleaVendorId) {
      continue;
    }
    if (!best || offer.priceRubles > best->price) {
      best = TraderOption{offer.vendorId, offer.vendorDisplayName, offer.priceRubles};
    }
  }

  return best;
}

std::optional<int64_t> fleaListingPrice(const Item& item) {
  if (item.lastLowPrice) {
    return std::max(kMinimumListPriceRubles, *item.lastLowPrice - kFleaUndercutRubles);
  }
  if (item.averagePriceLast24Hours) {
    return *item.averagePriceLast24Hours;
  }

  for (const auto& offer : item.sellFor) {
    if (offer.vendorId == kFleaVendorId) {
      return offer.priceRubles;
    }
  }

  return std::nullopt;
}

FleaOption fleaOption(const Item& item, const PlayerContext& context) {
  FleaOption flea;
  if (item.bannedFromFlea) {
    flea.available = false;
    flea.unavailableReason = payload::kFleaUnavailableReasonNoFlea;
    return flea;
  }

  std::optional<int64_t> listPrice = fleaListingPrice(item);
  if (!listPrice) {
    flea.available = false;
    flea.unavailableReason = payload::kFleaUnavailableReasonNoPrice;
    return flea;
  }

  flea.listPrice = *listPrice;
  flea.fee = fleaListingFee(item.basePrice, flea.listPrice, context.intelligenceCenterLevel);
  flea.net = flea.listPrice - flea.fee;

  if (!context.fleaUnlocked) {
    // Still report the numbers: "you'd get X once the flea unlocks" is useful.
    flea.available = false;
    flea.unavailableReason = payload::kFleaUnavailableReasonFleaLocked;
  }

  return flea;
}

std::vector<KeepReason> keepReasons(const Item& item, const HideoutRequirementsByItemId* hideout) {
  std::vector<KeepReason> reasons;
  for (const auto& task : item.usedInTasks) {
    reasons.push_back({task.name, KeepReason::Kind::Task, 1});
  }

  if (hideout) {
    auto requirements = hideout->find(item.id);
    if (requirements != hideout->end()) {
      for (const auto& requirement : requirements->second) {
        std::string stationAndLevel = requirement.stationName + " L" + std::to_string(requirement.level);
        reasons.push_back({stationAndLevel, KeepReason::Kind::Hideout, requirement.count});
      }
    }
  }

  return reasons;
}

}  // namespace

Recommendation recommend(const Item& item, const HideoutRequirementsByItemId* hideout, const PlayerContext& context) {
  Recommendation result;
  result.itemId = item.id;
  result.itemName = item.nameAndIcon.name;
  result.bestTrader = bestTraderOffer(item);
  result.flea = fleaOption(item, context);
  result.keepReasons = keepReasons(item, hideout);

  const int64_t traderPrice = result.bestTrader ? result.bestTrader->price : 0;
  const bool fleaUsable = result.flea->available && result.flea->net > 0;
  const double fleaMustPayMoreThan = static_cast<double>(traderPrice) * (1.0 + context.fleaMargin);
  const bool fleaWins = fleaUsable && static_cast<double>(result.flea->net) > fleaMustPayMoreThan;

  if (fleaWins) {
    result.verdict = Verdict::SellFlea;
    result.bestNetRubles = result.flea->net;
  } else if (result.bestTrader) {
    result.verdict = Verdict::SellTrader;
    result.bestNetRubles = traderPrice;
  } else if (fleaUsable) {
    result.verdict = Verdict::SellFlea;
    result.bestNetRubles = result.flea->net;
  } else {
    result.verdict = Verdict::NoBuyer;
  }

  // Keep overrides the sell path but leaves the net as "what selling would give".
  if (!result.keepReasons.empty() && !context.ignoreKeepReasons) {
    result.verdict = Verdict::Keep;
  }

  result.bestNetPerSlot = result.bestNetRubles / item.inventorySlots();

  return result;
}

std::string recommendationToJson(const Recommendation& recommendation) {
  JsonWriter writer;
  writer.beginObject();
  writer.key(payload::kFieldItemId).value(recommendation.itemId);
  writer.key(payload::kFieldItemName).value(recommendation.itemName);
  writer.key(payload::kFieldVerdict).value(verdictName(recommendation.verdict));
  writer.key(payload::kFieldBestNetRubles).value(recommendation.bestNetRubles);
  writer.key(payload::kFieldBestNetPerSlotRubles).value(recommendation.bestNetPerSlot);
  writer.key(payload::kFieldPricesAreStale).value(recommendation.pricesAreStale);

  writer.key(payload::kFieldBestTrader);
  if (const auto& trader = recommendation.bestTrader) {
    writer.beginObject();
    writer.key(payload::kFieldVendorId).value(trader->vendorId);
    writer.key(payload::kFieldVendorName).value(trader->vendorDisplayName);
    writer.key(payload::kFieldPriceRubles).value(trader->price);
    writer.endObject();
  } else {
    writer.null();
  }

  writer.key(payload::kFieldFlea);
  if (const auto& flea = recommendation.flea) {
    writer.beginObject();
    writer.key(payload::kFieldAvailable).value(flea->available);
    if (!flea->unavailableReason.empty()) {
      writer.key(payload::kFieldUnavailableReason).value(flea->unavailableReason);
    }
    writer.key(payload::kFieldListPriceRubles).value(flea->listPrice);
    writer.key(payload::kFieldFeeRubles).value(flea->fee);
    writer.key(payload::kFieldNetRubles).value(flea->net);
    writer.endObject();
  } else {
    writer.null();
  }

  writer.key(payload::kFieldKeepReasons).beginArray();
  for (const auto& reason : recommendation.keepReasons) {
    writer.beginObject();
    bool forTask = reason.kind == KeepReason::Kind::Task;
    writer.key(payload::kFieldKind).value(forTask ? payload::kKeepReasonKindTask : payload::kKeepReasonKindHideout);
    writer.key(payload::kFieldLabel).value(reason.taskOrStationLabel);
    writer.key(payload::kFieldCount).value(reason.count);
    writer.endObject();
  }
  writer.endArray();
  writer.endObject();

  return writer.takeText();
}

std::string summariesToJson(const std::vector<Item>& items) {
  JsonWriter writer;
  writer.beginArray();
  for (const auto& item : items) {
    writer.beginObject();
    writer.key(payload::kFieldId).value(item.id);
    writeNameAndIconKeys(writer, item.nameAndIcon);
    writer.key(payload::kFieldSlots).value(item.inventorySlots());
    writer.key(payload::kFieldAveragePriceLast24Hours);
    if (item.averagePriceLast24Hours) {
      writer.value(*item.averagePriceLast24Hours);
    } else {
      writer.null();
    }
    writer.endObject();
  }
  writer.endArray();

  return writer.takeText();
}

}  // namespace tarkov_companion
