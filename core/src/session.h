// The sans-IO session behind tarkov_companion_session_* (ADR 0006). Flows are
// continuations of pending actions; one mutex guards all state, and nothing
// calls out of the session, so there is no re-entrancy.
#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "authentication.h"
#include "bridge.h"
#include "clock.h"
#include "engine.h"
#include "favorites.h"
#include "json.h"
#include "session_config.h"
#include "session_types.h"

namespace tarkov_companion {

struct SessionClocks {
  WallClock wall = realWallClock();
  SteadyClock steady = realSteadyClock();
};

// Bulky content travels in `payload` as raw bytes, never escaped into `json`.
struct Action {
  std::string json;
  std::optional<std::string> payload;
  // Tagged so cancel() and closeBridge() can drop actions already queued.
  std::optional<RequestId> resultForRequest;
  std::optional<BridgeId> forBridge;
};

class Session final : private BridgeHost, private AuthenticationHost, private FavoritesHost {
 public:
  ~Session();

  // Hideout requirements change once per wipe; refreshing daily is plenty.
  static constexpr std::chrono::hours kHideoutTimeToLive{24};
  static constexpr int kSearchResultLimit = 20;
  static constexpr int kHttpTimeoutMilliseconds = 10000;
  // Request ids with this bit set belong to bridges; callers can't use them.
  static constexpr RequestId kBridgeRequestBit = RequestId{1} << 63;
  static constexpr const char* kRemoteFlagsCacheKey = "remote-flags";
  // After tarkov.dev's GraphQL API fails, reads go to the catalog first for
  // this long, so an outage doesn't cost every request a failed round trip.
  static constexpr std::chrono::minutes kGraphqlRetryAfter{10};

  static std::unique_ptr<Session> create(std::string_view configurationJson, std::string* error,
                                         SessionClocks clocks = {});

  bool setProfile(std::string_view profileJson, std::string* error);

  // Commands. Each produces exactly one "result" action for `request`, unless
  // cancelled. Return false only if `request` is already in use or reserved.
  bool search(RequestId request, std::string_view query);
  bool recommend(RequestId request, std::string_view itemId);

  // No result is produced for `request` afterwards, including one already
  // queued. Work in flight still completes and still fills the offline cache.
  void cancel(RequestId request);

  // One bridge per WebView connection: a page reload opens a new one.
  std::optional<BridgeId> openBridge(std::string_view optionsJson, std::string* error);
  bool bridgeReceive(BridgeId bridge, std::string_view message);
  bool bridgeEvent(BridgeId bridge, std::string_view name);
  void closeBridge(BridgeId bridge);

  // Returns false for a reserved or busy request id, or too few entropy bytes;
  // problems after that are reported in its result.
  bool startSignIn(RequestId request, const uint8_t* signInEntropyBytes, size_t entropyLength);
  void signInRedirect(std::string_view redirectUriOrEmptyIfClosed);
  bool signOut(RequestId request);

  bool listFavorites(RequestId request);
  bool setFavorite(RequestId request, std::string_view itemId, bool favorite);

  std::optional<Action> nextAction();

  // Both return false for an action id that is unknown or of the other kind.
  bool completeHttp(ActionId action, int statusCode, std::string_view body);
  bool completeStoredValueRead(ActionId action, std::optional<std::string_view> storedValue);

 private:
  struct PendingAction {
    HttpContinuation onHttp;
    StoredValueContinuation onStoredValueRead;
  };

  struct CacheEntry {
    SystemTime storedAt;
    std::string body;
  };

  struct ItemsFetch {
    std::optional<Failure> failure;
    std::string summariesJson;
    bool fromCache = false;
    int64_t cacheAgeSeconds = 0;
  };
  using ItemsContinuation = std::function<void(ItemsFetch)>;

  struct Upstream {
    std::string graphqlBody;
    std::string catalogFileUrlOrEmpty;
    // Unset when the catalog's file already is the GraphQL response it stands for.
    std::function<std::optional<std::string>(std::string_view)> catalogFileToGraphqlResponse;
  };
  using ParseAndStoreInEngine = std::function<ParseStatus(std::string_view)>;
  using FailureContinuation = std::function<void(std::optional<Failure>)>;

  Session(SessionConfiguration configuration, SessionClocks clocks);

  void fetchItems(const std::string& cacheKey, Upstream upstream, RequestId request, ItemsContinuation done);
  void fetchFromGraphqlThenCatalog(const std::string& cacheKey, Upstream upstream, ParseAndStoreInEngine parseAndStore,
                                   FailureContinuation done);
  void fetchFromCatalog(const std::string& cacheKey, Upstream upstream, ParseAndStoreInEngine parseAndStore,
                        FailureContinuation done, std::optional<Failure> graphqlFailure);
  void ensureHideout(std::function<void(bool ready)> done);
  void finishHideoutLoad(const std::string& cacheKey, std::optional<SystemTime> fetchedAtOrNothingLoaded);
  void recommendWithHideout(RequestId request, const std::string& itemId, bool hideoutReady);

  std::optional<Failure> failureOfHttpResponse(const HttpOutcome& response, const std::string& cacheKey);
  std::optional<Failure> failureOfRejectedResponse(ParseStatus status, const std::string& cacheKey);

  void startSearchWithLockHeld(RequestId request, std::string_view query);
  void startRecommendWithLockHeld(RequestId request, std::string_view itemId);
  void cancelWithLockHeld(RequestId request);

  // Shared by the host interfaces of all parts.
  void trackAnalyticsEvent(std::string event, AnalyticsProperties properties) override;
  void log(LogLevel level, std::string message) override;
  void finishWithPayload(RequestId request, std::string payloadJson) override;
  void finishWithError(RequestId request, const Failure& failure) override;

  // BridgeHost: called by bridges, with the lock held.
  void sendToWeb(BridgeId bridge, std::string message) override;
  void closeScreen(BridgeId bridge) override;
  void saveState(BridgeId bridge, const bridge::UiState& state) override;
  RequestId newRequest(BridgeId bridge) override;
  void startSearch(RequestId request, std::string_view query) override;
  void startRecommend(RequestId request, std::string_view itemId) override;
  void cancelRequest(RequestId request) override;
  const RemoteFlags& remoteFlags() const override;
  bool signInEnabled() const override;
  const std::optional<Account>& currentAccount() const override;
  void startSignInRequestedByPage(BridgeId bridge) override;
  void signOutRequestedByPage() override;
  void startFavoritesList(RequestId request) override;
  void startFavoriteSet(RequestId request, std::string_view itemId, bool favorite) override;

  // AuthenticationHost: called by sign-in, with the lock held.
  void sendRequest(HttpRequest request, HttpContinuation then) override;
  void readSecret(const std::string& key, StoredValueContinuation then) override;
  void writeSecret(const std::string& key, std::string value) override;
  void deleteSecret(const std::string& key) override;
  void openBrowser(RequestId request, const std::string& url) override;
  void notifyAccountChanged() override;
  void storedSignInRestored() override;

  // FavoritesHost: called by favourites, with the lock held.
  void readData(const std::string& key, StoredValueContinuation then) override;
  void writeData(const std::string& key, std::string value) override;
  void deleteData(const std::string& key) override;
  void notifyFavoritesChanged() override;
  std::optional<ItemNameAndIcon> itemNameAndIcon(const std::string& itemId) override;

  void queueBridgeAction(BridgeId bridge, std::string json, std::optional<std::string> payload = std::nullopt);

  bool beginRequest(RequestId request);
  bool isActive(RequestId request) const;
  Bridge* takeBridgeThatStarted(RequestId request);
  void finishWithRecommendation(RequestId request, const Recommendation& recommendation, bool hideoutReady);

  void loadRemoteFlags();
  void applyRemoteFlags(RemoteFlags flags);

  void sendGraphqlRequest(std::string body, HttpContinuation then);
  void readCache(const std::string& key, StoredValueContinuation then);
  void writeCache(const std::string& key, std::string_view body);
  void logUnknownKeys(const char* documentName, const std::vector<std::string>& keys);

  std::string cacheKey(std::string_view kind, std::string_view subject = {}) const;
  JsonWriter beginGraphqlRequest(const char* query) const;
  std::string catalogFileUrlOrEmpty(std::string_view pathInCatalog) const;
  std::optional<CacheEntry> decodeCacheEntry(std::optional<std::string_view> storedValue);
  bool isFresh(SystemTime dataTime, std::chrono::milliseconds timeToLive) const;

  // Ordered by alignment rather than by topic, so the object has no holes on
  // either 64- or 32-bit ABIs: m_engine needs m_configuration and m_steadyClock first.
  mutable std::mutex m_mutex;
  const SessionConfiguration m_configuration;
  WallClock m_wallClock;
  SteadyClock m_steadyClock;
  Engine m_engine;
  PlayerProfile m_profile;
  std::deque<Action> m_actions;
  // The last good flags document (defaults until one arrives).
  RemoteFlags m_remoteFlags;

  friend class SessionTestAccess;  // core/tests/authentication_test.cpp
  Authentication m_authentication{*this, m_configuration.signIn, m_wallClock};
  Favorites m_favorites{*this, m_authentication, m_configuration.favoritesSyncUrl, m_wallClock};

  ActionId m_nextActionId = 1;
  BridgeId m_nextBridgeId = 1;
  RequestId m_nextBridgeRequest = kBridgeRequestBit;
  SystemTime m_hideoutDataFetchedAt;
  SteadyTime m_skipGraphqlUntil;

  std::unordered_map<ActionId, PendingAction> m_pendingActionsById;
  std::unordered_set<RequestId> m_activeRequests;
  std::unordered_map<BridgeId, std::unique_ptr<Bridge>> m_bridgesById;
  std::unordered_map<RequestId, BridgeId> m_bridgeThatStartedRequest;
  std::string m_cacheKeyOfHideoutInEngine;
  std::unordered_map<std::string, std::vector<std::function<void(bool)>>> m_waitingForHideoutLoad;
};

}  // namespace tarkov_companion
