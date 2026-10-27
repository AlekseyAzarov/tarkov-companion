// Only what the recommender needs, so the parser tolerates upstream schema additions.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "contract_generated.h"

namespace tarkov_companion {

inline constexpr const char* kFleaVendorId = "flea-market";

struct SellOffer {
  std::string vendorId;
  std::string vendorDisplayName;
  int64_t priceRubles = 0;
};

struct TaskReference {
  std::string id;
  std::string name;
  int minimumPlayerLevel = 0;
};

struct ItemNameAndIcon {
  std::string name;
  std::string shortName;
  std::string iconLink;
};

struct Item {
  std::string id;
  ItemNameAndIcon nameAndIcon;
  int64_t basePrice = 0;
  std::optional<int64_t> averagePriceLast24Hours;
  std::optional<int64_t> lastLowPrice;
  int width = 1;
  int height = 1;
  bool bannedFromFlea = false;
  std::vector<SellOffer> sellFor;
  std::vector<TaskReference> usedInTasks;

  int inventorySlots() const { return width * height > 0 ? width * height : 1; }
};

struct HideoutRequirement {
  std::string stationName;
  int level = 0;
  int count = 0;
};

using HideoutRequirementsByItemId = std::unordered_map<std::string, std::vector<HideoutRequirement>>;

inline constexpr double kPercentInWhole = 100.0;
// How much more the flea must pay than a trader to be worth listing.
inline constexpr double kDefaultFleaMargin = static_cast<double>(contract::kFleaMarginPercentDefault) / kPercentInWhole;

struct PlayerContext {
  int playerLevel = static_cast<int>(contract::kPlayerLevelDefault);
  bool fleaUnlocked = true;
  int intelligenceCenterLevel = 0;
  bool ignoreKeepReasons = false;
  double fleaMargin = kDefaultFleaMargin;
};

enum class Verdict { Keep, SellFlea, SellTrader, NoBuyer };

const char* verdictName(Verdict verdict);

struct KeepReason {
  enum class Kind { Task, Hideout };

  std::string taskOrStationLabel;
  Kind kind;
  int count = 1;
};

struct TraderOption {
  std::string vendorId;
  std::string vendorDisplayName;
  int64_t price = 0;
};

struct FleaOption {
  int64_t listPrice = 0;
  int64_t fee = 0;
  int64_t net = 0;
  bool available = true;
  std::string unavailableReason;
};

struct Recommendation {
  std::string itemId;
  std::string itemName;
  std::optional<TraderOption> bestTrader;
  std::optional<FleaOption> flea;
  int64_t bestNetRubles = 0;
  int64_t bestNetPerSlot = 0;
  std::vector<KeepReason> keepReasons;
  Verdict verdict = Verdict::NoBuyer;
  bool pricesAreStale = false;
  // Reported next to the recommendation rather than inside its JSON: the
  // session puts it into the result payload as "fromCache".
  bool fromOfflineCache = false;
};

}  // namespace tarkov_companion
