#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "lru_cache.h"
#include "model.h"
#include "tarkov_parser.h"

namespace tarkov_companion {

inline constexpr size_t kDefaultItemCacheCapacity = static_cast<size_t>(contract::kItemCacheCapacityDefault);
inline constexpr std::chrono::seconds kPricesNeverGoStale{0};

struct EngineConfiguration {
  size_t itemCacheCapacity = kDefaultItemCacheCapacity;
  std::chrono::seconds itemTimeToLive = kPricesNeverGoStale;
};

// Verdicts built on offline-cache data are flagged as possibly outdated.
enum class DataSource { Network, OfflineCache };

// Thread-safe.
class Engine {
 public:
  explicit Engine(EngineConfiguration config, SteadyClock clock = realSteadyClock());

  ParseStatus parseAndStoreItems(std::string_view json, std::string* summariesJsonInResponseOrder,
                                 DataSource source = DataSource::Network);
  ParseStatus parseAndStoreHideout(std::string_view json);
  bool hasHideout() const;

  std::optional<Recommendation> recommend(const std::string& itemId, const PlayerContext& context);

  std::optional<ItemNameAndIcon> nameAndIcon(const std::string& itemId);

  size_t cachedItemCount() const;

 private:
  struct CatalogEntry {
    Item item;
    DataSource source = DataSource::Network;
  };

  mutable std::mutex m_mutex;
  LruCache<std::string, CatalogEntry> m_catalog;
  std::optional<HideoutRequirementsByItemId> m_hideoutRequirements;
};

}  // namespace tarkov_companion
