// Favourites (docs/favorites.md, ADR 0010): local first, the later change wins
// per item, removals kept as records. Runs with the session's lock held.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "authentication.h"
#include "clock.h"
#include "json.h"
#include "model.h"
#include "session_types.h"

namespace tarkov_companion {

// Called with the session's lock held.
class FavoritesHost {
 public:
  // The platform's data store: kept until deleted, unlike the cache.
  virtual void readData(const std::string& key, StoredValueContinuation then) = 0;
  virtual void writeData(const std::string& key, std::string value) = 0;
  virtual void deleteData(const std::string& key) = 0;
  virtual void finishWithPayload(RequestId request, std::string payloadJson) = 0;
  virtual void finishWithError(RequestId request, const Failure& failure) = 0;
  virtual void trackAnalyticsEvent(std::string event, AnalyticsProperties properties) = 0;
  virtual void log(LogLevel level, std::string message) = 0;
  virtual void notifyFavoritesChanged() = 0;
  // From the SDK's own tarkov.dev data, never from the caller: the page is untrusted.
  virtual std::optional<ItemNameAndIcon> itemNameAndIcon(const std::string& itemId) = 0;

 protected:
  ~FavoritesHost() = default;
};

class Favorites {
 public:
  Favorites(FavoritesHost& host, Authentication& authentication, const std::string& syncUrlOrEmpty,
            WallClock wallClock);

  // Calling it again for the same owner does nothing.
  void switchToOwner(const std::optional<Account>& accountOrThisDevice);

  void listFavorites(RequestId request);
  void setFavorite(RequestId request, std::string_view itemId, bool favorite);

  std::string favoritesJsonMostRecentFirst() const;

  // Nothing wakes the device for offline changes; the session calls this when the overlay opens.
  void syncIfChangesAreWaiting();

 private:
  struct Entry {
    std::string itemId;
    bool favorite = false;
    int64_t changedAtEpochMilliseconds = 0;
    ItemNameAndIcon nameAndIcon;
  };
  using EntriesByItemId = std::map<std::string, Entry>;

  void whenLoaded(std::function<void()> then);
  void load(const std::string& storageKey, EntriesByItemId mergedFromThisDevice);
  void sync();
  void applySyncResponse(const EntriesByItemId& sent, std::string_view body);
  void persist();
  void notifyChanged();
  bool signedIn() const;
  size_t favoriteCount() const;

  static std::string storageKeyFor(const std::optional<Account>& accountOrThisDevice);
  static std::string entriesJson(const EntriesByItemId& entries);
  static EntriesByItemId entriesFromJsonSkippingMalformed(const JsonValue& array);

  // Ordered by alignment rather than by topic, so the object has no holes on
  // either 64- or 32-bit ABIs.
  WallClock m_wallClock;
  int64_t m_serverCursor = 0;
  // Bumped on every switch, so answers for the previous owner are ignored.
  uint64_t m_generation = 0;
  FavoritesHost& m_host;
  Authentication& m_authentication;
  const std::string& m_syncUrlOrEmpty;
  std::string m_storageKeyOrEmptyBeforeFirstSwitch;
  std::vector<std::function<void()>> m_waitingForLoad;
  EntriesByItemId m_entriesIncludingRemovals;
  EntriesByItemId m_changesNotConfirmedByServer;
  bool m_loaded = false;
  bool m_syncing = false;
};

}  // namespace tarkov_companion
