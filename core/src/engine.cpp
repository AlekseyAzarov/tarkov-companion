#include "engine.h"

#include "recommender.h"

namespace tarkov_companion {

Engine::Engine(EngineConfiguration config, SteadyClock clock)
    : m_catalog(config.itemCacheCapacity, config.itemTimeToLive, std::move(clock)) {}

ParseStatus Engine::parseAndStoreItems(std::string_view json, std::string* summariesJsonInResponseOrder,
                                       DataSource source) {
  // Parse outside the engine's lock: it touches no shared state. Session calls
  // this from an HTTP or cache continuation, which runs under Session's own
  // mutex, so there the whole session waits for the parse. That's acceptable
  // because the Android layer drives the core from one thread.
  ItemsParseResult parsed = parseItemsResponse(json);
  if (parsed.status != ParseStatus::Ok) {
    return parsed.status;
  }
  if (summariesJsonInResponseOrder) {
    *summariesJsonInResponseOrder = summariesToJson(parsed.items);
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  for (auto& item : parsed.items) {
    std::string id = item.id;
    m_catalog.put(id, CatalogEntry{std::move(item), source});
  }

  return ParseStatus::Ok;
}

ParseStatus Engine::parseAndStoreHideout(std::string_view json) {
  HideoutParseResult parsed = parseHideoutResponse(json);
  if (parsed.status != ParseStatus::Ok) {
    return parsed.status;
  }

  std::lock_guard<std::mutex> lock(m_mutex);
  m_hideoutRequirements = std::move(parsed.requirementsByItemId);

  return ParseStatus::Ok;
}

bool Engine::hasHideout() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  return m_hideoutRequirements.has_value();
}

std::optional<Recommendation> Engine::recommend(const std::string& itemId, const PlayerContext& context) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto cached = m_catalog.get(itemId);
  if (!cached.value) {
    return std::nullopt;
  }

  const HideoutRequirementsByItemId* hideout = m_hideoutRequirements ? &m_hideoutRequirements.value() : nullptr;
  // Qualified: unqualified `recommend` would resolve to this member function.
  Recommendation result = tarkov_companion::recommend(cached.value->item, hideout, context);
  result.pricesAreStale = cached.expired;
  result.fromOfflineCache = cached.value->source == DataSource::OfflineCache;

  return result;
}

std::optional<ItemNameAndIcon> Engine::nameAndIcon(const std::string& itemId) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto cached = m_catalog.get(itemId);
  if (!cached.value) {
    return std::nullopt;
  }

  return cached.value->item.nameAndIcon;
}

size_t Engine::cachedItemCount() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  return m_catalog.size();
}

}  // namespace tarkov_companion
