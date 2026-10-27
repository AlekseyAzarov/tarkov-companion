#include "favorites.h"

#include <algorithm>
#include <utility>

#include "contract_generated.h"
#include "http.h"
#include "item_name_and_icon_json.h"
#include "text.h"

namespace tarkov_companion {
namespace {

constexpr int kHttpOk = 200;
namespace payload = contract::payload;
namespace sync = contract::favorites_sync;

// The stored list, in the data store: entries and the changes not yet
// confirmed by the server, in the same shape as sync changes.
constexpr const char* kStoredFieldEntries = "entries";
constexpr const char* kStoredFieldOutbox = "outbox";
constexpr const char* kStoredFieldCursor = "cursor";

constexpr const char* kThisDeviceStorageKey = "favorites|local";
constexpr const char* kAccountStorageKeyPrefix = "favorites|account|";


}  // namespace

Favorites::Favorites(FavoritesHost& host, Authentication& authentication, const std::string& syncUrlOrEmpty,
                     WallClock wallClock)
    : m_wallClock(std::move(wallClock)),
      m_host(host),
      m_authentication(authentication),
      m_syncUrlOrEmpty(syncUrlOrEmpty) {}

std::string Favorites::storageKeyFor(const std::optional<Account>& accountOrThisDevice) {
  return accountOrThisDevice ? kAccountStorageKeyPrefix + accountOrThisDevice->id : kThisDeviceStorageKey;
}

bool Favorites::signedIn() const {
  return text::startsWith(m_storageKeyOrEmptyBeforeFirstSwitch, kAccountStorageKeyPrefix);
}

size_t Favorites::favoriteCount() const {
  return static_cast<size_t>(std::count_if(m_entriesIncludingRemovals.begin(), m_entriesIncludingRemovals.end(),
                                           [](const auto& entry) { return entry.second.favorite; }));
}

// ---------------------------------------------------------------------------
// Whose list

void Favorites::switchToOwner(const std::optional<Account>& accountOrThisDevice) {
  std::string storageKey = storageKeyFor(accountOrThisDevice);
  if (storageKey == m_storageKeyOrEmptyBeforeFirstSwitch) {
    return;
  }

  bool signingIn = accountOrThisDevice.has_value() && m_storageKeyOrEmptyBeforeFirstSwitch == kThisDeviceStorageKey;
  bool signingOut = !accountOrThisDevice.has_value() && signedIn();
  // On sign-in this device's list joins the account; changes still queued
  // for the old owner are dropped with it.
  EntriesByItemId mergedFromThisDevice = signingIn ? m_entriesIncludingRemovals : EntriesByItemId{};
  // Signing out: the account's list leaves the device (it may be shared).
  if (signingOut) {
    m_host.deleteData(m_storageKeyOrEmptyBeforeFirstSwitch);
  }

  m_storageKeyOrEmptyBeforeFirstSwitch = std::move(storageKey);
  ++m_generation;
  m_syncing = false;
  m_loaded = false;
  m_entriesIncludingRemovals.clear();
  m_changesNotConfirmedByServer.clear();
  m_serverCursor = 0;

  load(m_storageKeyOrEmptyBeforeFirstSwitch, std::move(mergedFromThisDevice));
}

void Favorites::load(const std::string& storageKey, EntriesByItemId mergedFromThisDevice) {
  uint64_t generation = m_generation;
  m_host.readData(storageKey, [generation, this, mergedFromThisDevice = std::move(mergedFromThisDevice)](
                                     std::optional<std::string_view> storedValue) {
    if (generation != m_generation) {
      return;
    }

    if (storedValue) {
      JsonParseResult stored = parseJson(*storedValue);
      if (stored.ok) {
        m_entriesIncludingRemovals = entriesFromJsonSkippingMalformed(stored.value[kStoredFieldEntries]);
        m_changesNotConfirmedByServer = entriesFromJsonSkippingMalformed(stored.value[kStoredFieldOutbox]);
        m_serverCursor = stored.value[kStoredFieldCursor].asInteger(0);
      } else {
        m_host.log(LogLevel::Warn, "ignoring unreadable stored favourites");
      }
    }

    for (const auto& [itemId, localEntry] : mergedFromThisDevice) {
      auto existing = m_entriesIncludingRemovals.find(itemId);
      bool accountHasNewerChange = existing != m_entriesIncludingRemovals.end() &&
                                   existing->second.changedAtEpochMilliseconds >= localEntry.changedAtEpochMilliseconds;
      if (accountHasNewerChange) {
        continue;
      }
      m_entriesIncludingRemovals[itemId] = localEntry;
      m_changesNotConfirmedByServer[itemId] = localEntry;
    }
    if (!mergedFromThisDevice.empty()) {
      m_host.deleteData(kThisDeviceStorageKey);
      persist();
    }

    m_loaded = true;
    auto waiting = std::move(m_waitingForLoad);
    m_waitingForLoad.clear();
    for (auto& resume : waiting) {
      resume();
    }

    notifyChanged();
    sync();
  });
}

void Favorites::whenLoaded(std::function<void()> then) {
  if (m_loaded) {
    then();
  } else {
    m_waitingForLoad.push_back(std::move(then));
  }
}

// ---------------------------------------------------------------------------
// Commands

void Favorites::listFavorites(RequestId request) {
  whenLoaded([this, request] {
    JsonWriter result;
    result.beginObject().key(payload::kFieldItems).rawJson(favoritesJsonMostRecentFirst()).endObject();
    m_host.finishWithPayload(request, result.takeText());
  });
}

void Favorites::setFavorite(RequestId request, std::string_view rawItemId, bool favorite) {
  if (!text::isItemId(rawItemId)) {
    m_host.finishWithError(request, {ErrorCode::InvalidArgument, "bad item id"});
    return;
  }

  whenLoaded([this, request, itemId = std::string(rawItemId), favorite] {
    auto existing = m_entriesIncludingRemovals.find(itemId);
    bool isFavoriteAlready = existing != m_entriesIncludingRemovals.end() && existing->second.favorite;
    if (isFavoriteAlready == favorite) {
      m_host.finishWithPayload(request, kEmptyJsonObject);
      return;
    }

    Entry entry;
    if (existing != m_entriesIncludingRemovals.end()) {
      entry = existing->second;
    } else {
      entry.itemId = itemId;
    }

    if (favorite) {
      if (favoriteCount() >= static_cast<size_t>(contract::kFavoritesMaximum)) {
        std::string limit = std::to_string(contract::kFavoritesMaximum);
        m_host.finishWithError(request, {ErrorCode::InvalidArgument, "favourites are full (" + limit + ")"});
        return;
      }

      std::optional<ItemNameAndIcon> nameAndIcon = m_host.itemNameAndIcon(itemId);
      if (!nameAndIcon) {
        m_host.finishWithError(request, {ErrorCode::NotFound, "unknown item " + itemId});
        return;
      }

      entry.nameAndIcon = std::move(*nameAndIcon);
    }

    entry.favorite = favorite;
    // Later than anything known for the item, even if the clock went back.
    int64_t now = millisecondsSinceEpoch(m_wallClock());
    entry.changedAtEpochMilliseconds = std::max(now, entry.changedAtEpochMilliseconds + 1);

    m_entriesIncludingRemovals[itemId] = entry;
    if (signedIn()) {
      m_changesNotConfirmedByServer[itemId] = entry;
    }
    persist();
    notifyChanged();

    m_host.trackAnalyticsEvent(contract::kEventFavoriteToggled,
                                  {{contract::kPropertyFavorite, jsonBooleanText(favorite)},
                                   {contract::kPropertySignedIn, jsonBooleanText(signedIn())}});
    m_host.finishWithPayload(request, kEmptyJsonObject);
    sync();
  });
}

std::string Favorites::favoritesJsonMostRecentFirst() const {
  std::vector<const Entry*> favorites;
  for (const auto& [itemId, entry] : m_entriesIncludingRemovals) {
    if (entry.favorite) {
      favorites.push_back(&entry);
    }
  }
  std::sort(favorites.begin(), favorites.end(), [](const Entry* left, const Entry* right) {
    return left->changedAtEpochMilliseconds > right->changedAtEpochMilliseconds;
  });

  JsonWriter json;
  json.beginArray();
  for (const Entry* entry : favorites) {
    json.beginObject().key(payload::kFieldId).value(entry->itemId);
    writeNameAndIconKeys(json, entry->nameAndIcon).endObject();
  }
  json.endArray();

  return json.takeText();
}

// ---------------------------------------------------------------------------
// Sync

void Favorites::syncIfChangesAreWaiting() {
  if (!m_changesNotConfirmedByServer.empty()) {
    sync();
  }
}

// One request: our changes since the last sync, and everyone else's back.
void Favorites::sync() {
  if (!signedIn() || !m_loaded || m_syncUrlOrEmpty.empty() || m_syncing) {
    return;
  }

  m_syncing = true;
  EntriesByItemId sent = m_changesNotConfirmedByServer;
  uint64_t generation = m_generation;
  JsonWriter body;
  body.beginObject()
      .key(sync::kFieldSince).value(m_serverCursor)
      .key(sync::kFieldChanges).rawJson(entriesJson(sent))
      .endObject();
  HttpRequest syncRequest{http::kMethodPost, m_syncUrlOrEmpty, {{http::kHeaderContentType, http::kContentTypeJson}},
                          body.takeText()};

  m_authentication.sendWithAccessToken(std::move(syncRequest), [this, sent, generation](
                                                                             std::optional<Failure> failure,
                                                                             const HttpOutcome& response) {
    if (generation != m_generation) {
      // Signed out or in as someone else meanwhile.
      return;
    }

    m_syncing = false;
    if (failure) {
      // Offline, or signed out by an expired session: the queued changes
      // stay and go with the next sync.
      m_host.log(LogLevel::Debug, "favourites sync postponed: " + failure->message);
      return;
    }
    if (response.statusCode != kHttpOk) {
      m_host.log(LogLevel::Warn, "favourites sync failed (HTTP " + std::to_string(response.statusCode) + ")");
      return;
    }

    applySyncResponse(sent, response.body);
    bool changedWhileThisSyncWasInFlight = !m_changesNotConfirmedByServer.empty();
    if (changedWhileThisSyncWasInFlight) {
      sync();
    }
  });
}

void Favorites::applySyncResponse(const EntriesByItemId& sent, std::string_view body) {
  JsonParseResult parsed = parseJson(body);
  if (!parsed.ok || !parsed.value[sync::kFieldChanges].isArray()) {
    m_host.log(LogLevel::Warn, "favourites sync: unexpected response");
    return;
  }

  // Sent and not changed since: the server has it now.
  for (const auto& [itemId, sentEntry] : sent) {
    auto queued = m_changesNotConfirmedByServer.find(itemId);
    bool unchangedSinceSent = queued != m_changesNotConfirmedByServer.end() &&
                              queued->second.changedAtEpochMilliseconds == sentEntry.changedAtEpochMilliseconds;
    if (unchangedSinceSent) {
      m_changesNotConfirmedByServer.erase(queued);
    }
  }

  bool anythingChanged = false;
  for (const auto& [itemId, serverRecord] : entriesFromJsonSkippingMalformed(parsed.value[sync::kFieldChanges])) {
    if (m_changesNotConfirmedByServer.count(itemId)) {
      // A newer local change goes with the next sync.
      continue;
    }

    Entry merged = serverRecord;
    auto existing = m_entriesIncludingRemovals.find(itemId);
    if (existing != m_entriesIncludingRemovals.end() && merged.nameAndIcon.name.empty()) {
      merged.nameAndIcon = existing->second.nameAndIcon;
    }

    bool differs = existing == m_entriesIncludingRemovals.end() || existing->second.favorite != merged.favorite ||
                   existing->second.changedAtEpochMilliseconds != merged.changedAtEpochMilliseconds;
    m_entriesIncludingRemovals[itemId] = merged;
    anythingChanged = anythingChanged || differs;
  }

  m_serverCursor = parsed.value[sync::kFieldCursor].asInteger(m_serverCursor);
  persist();
  if (anythingChanged) {
    notifyChanged();
  }
}

// ---------------------------------------------------------------------------
// Storage and notifications

void Favorites::persist() {
  JsonWriter json;
  json.beginObject()
      .key(kStoredFieldCursor).value(m_serverCursor)
      .key(kStoredFieldEntries).rawJson(entriesJson(m_entriesIncludingRemovals))
      .key(kStoredFieldOutbox).rawJson(entriesJson(m_changesNotConfirmedByServer))
      .endObject();
  m_host.writeData(m_storageKeyOrEmptyBeforeFirstSwitch, json.takeText());
}

void Favorites::notifyChanged() { m_host.notifyFavoritesChanged(); }

std::string Favorites::entriesJson(const EntriesByItemId& entries) {
  JsonWriter json;
  json.beginArray();
  for (const auto& [itemId, entry] : entries) {
    json.beginObject()
        .key(sync::kFieldItemId).value(itemId)
        .key(sync::kFieldFavorite).value(entry.favorite)
        .key(sync::kFieldChangedAt).value(entry.changedAtEpochMilliseconds);
    writeNameAndIconKeys(json, entry.nameAndIcon).endObject();
  }
  json.endArray();

  return json.takeText();
}

// Stored data and server answers are both input this code didn't write.
Favorites::EntriesByItemId Favorites::entriesFromJsonSkippingMalformed(const JsonValue& array) {
  EntriesByItemId entries;
  for (const JsonValue& item : array.items()) {
    std::string itemId(item[sync::kFieldItemId].asString());
    if (!text::isItemId(itemId) || !item[sync::kFieldFavorite].isBool()) {
      continue;
    }

    entries[itemId] =
        Entry{itemId, item[sync::kFieldFavorite].asBool(), item[sync::kFieldChangedAt].asInteger(0),
              nameAndIconFromJson(item)};
  }

  return entries;
}

}  // namespace tarkov_companion
