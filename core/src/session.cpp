#include "session.h"

#include <algorithm>
#include <charconv>
#include <system_error>
#include <utility>

#include "catalog.h"
#include "contract_generated.h"
#include "http.h"
#include "recommender.h"
#include "tarkov_api.h"
#include "text.h"

#ifndef TARKOV_COMPANION_VERSION_STRING
#define TARKOV_COMPANION_VERSION_STRING "0.0.0-dev"
#endif

namespace tarkov_companion {
namespace {

using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::seconds;

namespace action = contract::action;
namespace payload = contract::payload;

constexpr int kNoResponse = HttpOutcome::kNoResponseStatusCodeWithReasonInBody;
constexpr char kCacheEntryTimestampEnd = '\n';
constexpr const char* kCacheKeySeparator = "|";
constexpr const char* kCacheKindSearch = "search";
constexpr const char* kCacheKindItem = "item";
constexpr const char* kCacheKindHideout = "hideout";

const char* logLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug: return action::kLogLevelDebug;
    case LogLevel::Info: return action::kLogLevelInfo;
    case LogLevel::Warn: return action::kLogLevelWarn;
    case LogLevel::Error: return action::kLogLevelError;
  }

  return action::kLogLevelInfo;
}

const char* parseStatusName(ParseStatus status) {
  switch (status) {
    case ParseStatus::Ok: return "ok";
    case ParseStatus::InvalidJson: return "invalid_json";
    case ParseStatus::Schema: return "schema_mismatch";
    case ParseStatus::Api: return "api_error";
  }

  return "unknown";
}

bool isReservedForBridges(RequestId request) { return (request & Session::kBridgeRequestBit) != 0; }

}  // namespace

const char* errorCodeName(ErrorCode code) {
  switch (code) {
    case ErrorCode::InvalidArgument: return contract::kErrorCodeInvalidArgument;
    case ErrorCode::Network: return contract::kErrorCodeNetwork;
    case ErrorCode::Api: return contract::kErrorCodeApi;
    case ErrorCode::SchemaMismatch: return contract::kErrorCodeSchemaMismatch;
    case ErrorCode::NotFound: return contract::kErrorCodeNotFound;
    case ErrorCode::Internal: return contract::kErrorCodeInternal;
    case ErrorCode::SignInCancelled: return contract::kErrorCodeSignInCancelled;
    case ErrorCode::SignInFailed: return contract::kErrorCodeSignInFailed;
    case ErrorCode::NotSignedIn: return contract::kErrorCodeNotSignedIn;
  }

  return contract::kErrorCodeInternal;
}

std::unique_ptr<Session> Session::create(std::string_view configurationJson, std::string* error,
                                         SessionClocks clocks) {
  auto parsed = parseSessionConfiguration(configurationJson);
  if (!parsed.ok()) {
    if (error) {
      *error = parsed.error;
    }
    return nullptr;
  }

  std::unique_ptr<Session> session(new Session(std::move(parsed.settings), std::move(clocks)));
  session->logUnknownKeys("config", parsed.unknownKeys);
  if (!session->m_configuration.remoteFlagsUrl.empty()) {
    session->loadRemoteFlags();
  }
  if (session->m_authentication.enabled()) {
    // The favourites follow once it knows who is signed in.
    session->m_authentication.restoreStoredSignIn();
  } else {
    session->m_favorites.switchToOwner(std::nullopt);
  }

  return session;
}

Session::~Session() = default;

Session::Session(SessionConfiguration configuration, SessionClocks clocks)
    : m_configuration(std::move(configuration)),
      m_wallClock(std::move(clocks.wall)),
      m_steadyClock(std::move(clocks.steady)),
      m_engine(EngineConfiguration{m_configuration.itemCacheCapacity, m_configuration.priceTimeToLive},
               m_steadyClock) {}

bool Session::setProfile(std::string_view profileJson, std::string* error) {
  auto parsed = parsePlayerProfile(profileJson);
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!parsed.ok()) {
    if (error) {
      *error = parsed.error;
    }
    return false;
  }

  m_profile = parsed.settings;
  logUnknownKeys("profile", parsed.unknownKeys);

  return true;
}

// ---------------------------------------------------------------------------
// Commands

bool Session::search(RequestId request, std::string_view query) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (isReservedForBridges(request) || !beginRequest(request)) {
    return false;
  }
  startSearchWithLockHeld(request, query);

  return true;
}

bool Session::recommend(RequestId request, std::string_view itemId) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (isReservedForBridges(request) || !beginRequest(request)) {
    return false;
  }
  startRecommendWithLockHeld(request, itemId);

  return true;
}

void Session::cancel(RequestId request) {
  std::lock_guard<std::mutex> lock(m_mutex);
  cancelWithLockHeld(request);
}

void Session::startSearchWithLockHeld(RequestId request, std::string_view rawQuery) {
  std::string_view query = text::trimAsciiSpace(rawQuery);
  size_t length = text::codePointCount(query);
  if (length < static_cast<size_t>(contract::kSearchQueryMinimumLength) ||
      length > static_cast<size_t>(contract::kSearchQueryMaximumLength)) {
    std::string allowedLength = std::to_string(contract::kSearchQueryMinimumLength) + ".." +
                                std::to_string(contract::kSearchQueryMaximumLength);
    finishWithError(request, {ErrorCode::InvalidArgument, "query must be " + allowedLength + " characters"});
    return;
  }

  JsonWriter body = beginGraphqlRequest(contract::kQueryItemSearch);
  body.key(tarkov_api::kVariableName).value(query)
      .key(tarkov_api::kVariableLimit).value(kSearchResultLimit)
      .endObject()
      .endObject();
  // The catalog has no search engine: the query's shard is filtered here.
  std::optional<std::string> shardFileName = catalog::searchShardFileName(query);
  std::string shardUrl =
      shardFileName ? catalogFileUrlOrEmpty(tarkov_api::kCatalogSearchDirectory + *shardFileName) : std::string();
  Upstream upstream{body.takeText(), shardUrl,
                    [query = std::string(query)](std::string_view shardFile) {
                      return catalog::searchResponseFromShard(shardFile, query, kSearchResultLimit);
                    }};

  ItemsContinuation done = [this, request](ItemsFetch fetch) {
    if (fetch.failure) {
      finishWithError(request, *fetch.failure);
      return;
    }

    JsonWriter payload;
    payload.beginObject()
        .key(payload::kFieldItems).rawJson(fetch.summariesJson)
        .key(payload::kFieldFromCache).value(fetch.fromCache)
        .key(payload::kFieldCacheAgeSeconds).value(fetch.cacheAgeSeconds)
        .endObject();
    finishWithPayload(request, payload.takeText());
  };
  fetchItems(cacheKey(kCacheKindSearch, text::asciiLowercase(query)), std::move(upstream), request, std::move(done));
}

void Session::startRecommendWithLockHeld(RequestId request, std::string_view itemId) {
  if (!text::isItemId(itemId)) {
    finishWithError(request, {ErrorCode::InvalidArgument, "bad item id"});
    return;
  }

  ensureHideout([request, this, id = std::string(itemId)](bool hideoutReady) {
    recommendWithHideout(request, id, hideoutReady);
  });
}

void Session::cancelWithLockHeld(RequestId request) {
  m_activeRequests.erase(request);
  m_bridgeThatStartedRequest.erase(request);
  m_actions.erase(std::remove_if(m_actions.begin(), m_actions.end(),
                                 [request](const Action& action) { return action.resultForRequest == request; }),
                  m_actions.end());
}

// ---------------------------------------------------------------------------
// Bridges

std::optional<BridgeId> Session::openBridge(std::string_view optionsJson, std::string* error) {
  auto parsed = parseBridgeOptions(optionsJson);
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!parsed.ok()) {
    if (error) {
      *error = parsed.error;
    }
    return std::nullopt;
  }

  logUnknownKeys("bridge options", parsed.unknownKeys);
  BridgeId id = m_nextBridgeId++;
  std::unique_ptr<Bridge> bridge(
      new Bridge(id, std::move(parsed.settings), *this, m_steadyClock, TARKOV_COMPANION_VERSION_STRING));
  m_bridgesById.emplace(id, std::move(bridge));
  // The player is back in the overlay, likely online again.
  m_favorites.syncIfChangesAreWaiting();

  return id;
}

bool Session::bridgeReceive(BridgeId bridge, std::string_view message) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto found = m_bridgesById.find(bridge);
  if (found == m_bridgesById.end()) {
    return false;
  }
  found->second->receive(message);

  return true;
}

bool Session::bridgeEvent(BridgeId bridge, std::string_view name) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto found = m_bridgesById.find(bridge);
  if (found == m_bridgesById.end()) {
    return false;
  }
  found->second->sendEventOnceHandshakeIsDone(name);

  return true;
}

void Session::closeBridge(BridgeId bridge) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto found = m_bridgesById.find(bridge);
  if (found == m_bridgesById.end()) {
    return;
  }

  found->second->cancelEverythingAndGoSilent();
  m_bridgesById.erase(found);
  m_actions.erase(std::remove_if(m_actions.begin(), m_actions.end(),
                                 [bridge](const Action& action) { return action.forBridge == bridge; }),
                  m_actions.end());
}

void Session::sendToWeb(BridgeId bridge, std::string message) {
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeBridgeSend)
      .key(action::kFieldBridge).value(static_cast<int64_t>(bridge))
      .endObject();
  queueBridgeAction(bridge, json.takeText(), std::move(message));
}

void Session::closeScreen(BridgeId bridge) {
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeBridgeClose)
      .key(action::kFieldBridge).value(static_cast<int64_t>(bridge))
      .endObject();
  queueBridgeAction(bridge, json.takeText());
}

void Session::saveState(BridgeId bridge, const bridge::UiState& state) {
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeBridgeState)
      .key(action::kFieldBridge).value(static_cast<int64_t>(bridge))
      .key(action::kFieldRoute).value(state.route);
  if (!state.query.empty()) {
    json.key(action::kFieldQuery).value(state.query);
  }
  if (!state.itemId.empty()) {
    json.key(action::kFieldItemId).value(state.itemId);
  }
  json.endObject();

  queueBridgeAction(bridge, json.takeText());
}

void Session::trackAnalyticsEvent(std::string event, AnalyticsProperties properties) {
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeTrack)
      .key(action::kFieldEvent).value(event)
      .key(action::kFieldProperties).beginObject();
  for (const auto& [name, value] : properties) {
    json.key(name).value(value);
  }
  json.endObject().endObject();

  m_actions.push_back(Action{json.takeText(), std::nullopt, std::nullopt, std::nullopt});
}

void Session::startSearch(RequestId request, std::string_view query) { startSearchWithLockHeld(request, query); }

void Session::startRecommend(RequestId request, std::string_view itemId) {
  startRecommendWithLockHeld(request, itemId);
}

void Session::cancelRequest(RequestId request) { cancelWithLockHeld(request); }

RequestId Session::newRequest(BridgeId bridge) {
  RequestId request = m_nextBridgeRequest++;
  beginRequest(request);
  m_bridgeThatStartedRequest[request] = bridge;

  return request;
}

void Session::queueBridgeAction(BridgeId bridge, std::string json, std::optional<std::string> payload) {
  m_actions.push_back(Action{std::move(json), std::move(payload), std::nullopt, bridge});
}

std::optional<Action> Session::nextAction() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_actions.empty()) {
    return std::nullopt;
  }

  Action action = std::move(m_actions.front());
  m_actions.pop_front();

  return action;
}

bool Session::completeHttp(ActionId action, int statusCode, std::string_view body) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto found = m_pendingActionsById.find(action);
  if (found == m_pendingActionsById.end() || !found->second.onHttp) {
    return false;
  }

  HttpContinuation then = std::move(found->second.onHttp);
  // Before running `then`, which may add pending actions.
  m_pendingActionsById.erase(found);
  then(HttpOutcome{statusCode, body});

  return true;
}

bool Session::completeStoredValueRead(ActionId action, std::optional<std::string_view> storedValue) {
  std::lock_guard<std::mutex> lock(m_mutex);
  auto found = m_pendingActionsById.find(action);
  if (found == m_pendingActionsById.end() || !found->second.onStoredValueRead) {
    return false;
  }

  StoredValueContinuation then = std::move(found->second.onStoredValueRead);
  m_pendingActionsById.erase(found);
  then(storedValue);

  return true;
}

// ---------------------------------------------------------------------------
// Flows

// Network first; on a failure or a rejected response, the last good cached one, flagged.
void Session::fetchItems(const std::string& key, Upstream upstream, RequestId request, ItemsContinuation done) {
  auto summaries = std::make_shared<std::string>();
  ParseAndStoreInEngine parseAndStore = [this, summaries](std::string_view body) {
    return m_engine.parseAndStoreItems(body, summaries.get(), DataSource::Network);
  };

  FailureContinuation fetched = [this, key, request, done, summaries](std::optional<Failure> failure) {
    if (!failure) {
      done(ItemsFetch{std::nullopt, std::move(*summaries), false, 0});
      return;
    }
    // The fetch above still filled the cache for a cancelled request; a
    // fallback read would only serve a result nobody waits for.
    if (!isActive(request)) {
      return;
    }

    readCache(key, [this, done, failure](std::optional<std::string_view> storedValue) {
      std::optional<CacheEntry> cached = decodeCacheEntry(storedValue);
      std::string cachedSummaries;
      bool cacheUsable =
          cached &&
          m_engine.parseAndStoreItems(cached->body, &cachedSummaries, DataSource::OfflineCache) == ParseStatus::Ok;
      if (!cacheUsable) {
        done(ItemsFetch{failure, {}, false, 0});
        return;
      }

      // A store time in the future means the clock was moved back: the age is unknown, report 0.
      auto age = duration_cast<seconds>(m_wallClock() - cached->storedAt);
      int64_t ageSeconds = std::max<int64_t>(0, static_cast<int64_t>(age.count()));
      done(ItemsFetch{std::nullopt, std::move(cachedSummaries), true, ageSeconds});
    });
  };

  fetchFromGraphqlThenCatalog(key, std::move(upstream), std::move(parseAndStore), std::move(fetched));
}

// GraphQL first, then the catalog (docs/catalog.md); for a while after a
// GraphQL failure, the catalog straight away.
void Session::fetchFromGraphqlThenCatalog(const std::string& key, Upstream upstream,
                                          ParseAndStoreInEngine parseAndStore, FailureContinuation done) {
  bool hasCatalog = !upstream.catalogFileUrlOrEmpty.empty();
  if (hasCatalog && m_steadyClock() < m_skipGraphqlUntil) {
    fetchFromCatalog(key, std::move(upstream), std::move(parseAndStore), std::move(done), std::nullopt);
    return;
  }

  std::string body = std::move(upstream.graphqlBody);
  auto onGraphqlResponse = [this, key, upstream = std::move(upstream), parseAndStore = std::move(parseAndStore),
                            done = std::move(done), hasCatalog](const HttpOutcome& response) mutable {
    std::optional<Failure> failure = failureOfHttpResponse(response, key);
    if (!failure) {
      ParseStatus status = parseAndStore(response.body);
      if (status == ParseStatus::Ok) {
        writeCache(key, response.body);
        done(std::nullopt);
        return;
      }
      failure = failureOfRejectedResponse(status, key);
    }

    if (!hasCatalog) {
      done(failure);
      return;
    }

    if (m_steadyClock() >= m_skipGraphqlUntil) {
      log(LogLevel::Info, "tarkov.dev's GraphQL API failed; reading the catalog for the next " +
                              std::to_string(kGraphqlRetryAfter.count()) + " minutes");
    }
    m_skipGraphqlUntil = m_steadyClock() + kGraphqlRetryAfter;
    fetchFromCatalog(key, std::move(upstream), std::move(parseAndStore), std::move(done), std::move(failure));
  };

  sendGraphqlRequest(std::move(body), std::move(onGraphqlResponse));
}

void Session::fetchFromCatalog(const std::string& key, Upstream upstream, ParseAndStoreInEngine parseAndStore,
                               FailureContinuation done, std::optional<Failure> graphqlFailure) {
  HttpRequest request{http::kMethodGet, upstream.catalogFileUrlOrEmpty, {}, std::nullopt};
  auto onCatalogResponse = [this, key, upstream = std::move(upstream), parseAndStore = std::move(parseAndStore),
                            done = std::move(done),
                            graphqlFailure = std::move(graphqlFailure)](const HttpOutcome& response) {
    std::optional<Failure> failure = failureOfHttpResponse(response, key);
    if (!failure) {
      std::optional<std::string> body = upstream.catalogFileToGraphqlResponse
                                            ? upstream.catalogFileToGraphqlResponse(response.body)
                                            : std::string(response.body);
      ParseStatus status = body ? parseAndStore(*body) : ParseStatus::Schema;
      if (status == ParseStatus::Ok) {
        writeCache(key, *body);
        done(std::nullopt);
        return;
      }
      failure = failureOfRejectedResponse(status, key);
    }

    // When both fail, the GraphQL failure is the one that explains the outage.
    done(graphqlFailure ? graphqlFailure : failure);
  };

  sendRequest(std::move(request), std::move(onCatalogResponse));
}

// Fresh cache, else network, else stale cache. Concurrent callers share one
// load; a failure only drops the hideout's keep reasons.
void Session::ensureHideout(std::function<void(bool ready)> done) {
  std::string key = cacheKey(kCacheKindHideout);
  if (m_cacheKeyOfHideoutInEngine == key && isFresh(m_hideoutDataFetchedAt, kHideoutTimeToLive)) {
    done(true);
    return;
  }

  auto& waiting = m_waitingForHideoutLoad[key];
  waiting.push_back(std::move(done));
  bool loadAlreadyRunning = waiting.size() > 1;
  if (loadAlreadyRunning) {
    return;
  }

  readCache(key, [this, key](std::optional<std::string_view> storedValue) {
    std::optional<CacheEntry> cached = decodeCacheEntry(storedValue);
    if (cached && isFresh(cached->storedAt, kHideoutTimeToLive) &&
        m_engine.parseAndStoreHideout(cached->body) == ParseStatus::Ok) {
      finishHideoutLoad(key, cached->storedAt);
      return;
    }

    JsonWriter body = beginGraphqlRequest(contract::kQueryHideout);
    body.endObject().endObject();
    Upstream upstream{body.takeText(), catalogFileUrlOrEmpty(tarkov_api::kCatalogHideoutFile), nullptr};
    ParseAndStoreInEngine parseAndStore = [this](std::string_view response) {
      return m_engine.parseAndStoreHideout(response);
    };

    fetchFromGraphqlThenCatalog(key, std::move(upstream), std::move(parseAndStore),
                                [this, key, cached](std::optional<Failure> failure) {
      if (!failure) {
        finishHideoutLoad(key, m_wallClock());
        return;
      }
      // Day-old hideout data beats none: requirements change once per wipe.
      if (cached && m_engine.parseAndStoreHideout(cached->body) == ParseStatus::Ok) {
        finishHideoutLoad(key, cached->storedAt);
        return;
      }

      finishHideoutLoad(key, std::nullopt);
    });
  });
}

void Session::finishHideoutLoad(const std::string& key, std::optional<SystemTime> fetchedAtOrNothingLoaded) {
  if (fetchedAtOrNothingLoaded) {
    m_cacheKeyOfHideoutInEngine = key;
    m_hideoutDataFetchedAt = *fetchedAtOrNothingLoaded;
  }

  auto waiting = std::move(m_waitingForHideoutLoad[key]);
  m_waitingForHideoutLoad.erase(key);
  for (auto& resume : waiting) {
    resume(fetchedAtOrNothingLoaded.has_value());
  }
}

void Session::recommendWithHideout(RequestId request, const std::string& itemId, bool hideoutReady) {
  if (!isActive(request)) {
    return;
  }

  if (auto recommendation = m_engine.recommend(itemId, m_profile.context)) {
    finishWithRecommendation(request, *recommendation, hideoutReady);
    return;
  }

  // Not in the in-memory catalog: evicted, or the process was restarted and
  // the UI restored a previously opened item. Fetch it by id.
  JsonWriter body = beginGraphqlRequest(contract::kQueryItemById);
  body.key(tarkov_api::kVariableIds).beginArray().value(itemId).endArray().endObject().endObject();
  std::string itemFile = tarkov_api::kCatalogItemsDirectory + itemId + tarkov_api::kCatalogFileExtension;
  Upstream upstream{body.takeText(), catalogFileUrlOrEmpty(itemFile), nullptr};

  fetchItems(cacheKey(kCacheKindItem, itemId), std::move(upstream), request,
             [this, request, itemId, hideoutReady](ItemsFetch fetch) {
               if (fetch.failure) {
                 finishWithError(request, *fetch.failure);
                 return;
               }

               auto recommendation = m_engine.recommend(itemId, m_profile.context);
               if (!recommendation) {
                 finishWithError(request, {ErrorCode::NotFound, "unknown item " + itemId});
                 return;
               }

               finishWithRecommendation(request, *recommendation, hideoutReady);
             });
}

// ---------------------------------------------------------------------------
// Response handling

std::optional<Failure> Session::failureOfHttpResponse(const HttpOutcome& response, const std::string& key) {
  if (response.statusCode == kNoResponse) {
    log(LogLevel::Warn, "fetch failed for '" + key + "': " + std::string(response.body));
    return Failure{ErrorCode::Network, "network unavailable: " + std::string(response.body)};
  }
  if (!http::isSuccessStatus(response.statusCode)) {
    std::string status = "HTTP " + std::to_string(response.statusCode);
    log(LogLevel::Warn, "fetch failed for '" + key + "': " + status);
    return Failure{ErrorCode::Api, "tarkov.dev returned " + status};
  }

  return std::nullopt;
}

std::optional<Failure> Session::failureOfRejectedResponse(ParseStatus status, const std::string& key) {
  // Logged as an error: a schema change upstream needs a fix on our side.
  log(LogLevel::Error, "response rejected for '" + key + "': " + parseStatusName(status));
  if (status == ParseStatus::Api) {
    return Failure{ErrorCode::Api, "tarkov.dev reported an error"};
  }

  return Failure{ErrorCode::SchemaMismatch, "unexpected response shape"};
}

// ---------------------------------------------------------------------------
// Requests and results

bool Session::beginRequest(RequestId request) { return m_activeRequests.insert(request).second; }

bool Session::isActive(RequestId request) const { return m_activeRequests.count(request) != 0; }

Bridge* Session::takeBridgeThatStarted(RequestId request) {
  auto owner = m_bridgeThatStartedRequest.find(request);
  if (owner == m_bridgeThatStartedRequest.end()) {
    return nullptr;
  }

  auto bridge = m_bridgesById.find(owner->second);
  m_bridgeThatStartedRequest.erase(owner);

  return bridge == m_bridgesById.end() ? nullptr : bridge->second.get();
}

void Session::finishWithPayload(RequestId request, std::string payloadJson) {
  bool wasCancelled = m_activeRequests.erase(request) == 0;
  if (wasCancelled) {
    return;
  }
  if (Bridge* bridge = takeBridgeThatStarted(request)) {
    bridge->onSuccess(request, payloadJson);
    return;
  }

  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeResult)
      .key(action::kFieldRequest).value(static_cast<int64_t>(request))
      .key(action::kFieldOk).value(true)
      .endObject();
  m_actions.push_back(Action{json.takeText(), std::move(payloadJson), request, std::nullopt});
}

void Session::finishWithError(RequestId request, const Failure& failure) {
  bool wasCancelled = m_activeRequests.erase(request) == 0;
  if (wasCancelled) {
    return;
  }
  if (Bridge* bridge = takeBridgeThatStarted(request)) {
    bridge->onFailure(request, failure);
    return;
  }

  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeResult)
      .key(action::kFieldRequest).value(static_cast<int64_t>(request))
      .key(action::kFieldOk).value(false)
      .key(action::kFieldError).beginObject()
          .key(action::kFieldCode).value(errorCodeName(failure.code))
          .key(action::kFieldMessage).value(failure.message)
      .endObject()
      .endObject();
  m_actions.push_back(Action{json.takeText(), std::nullopt, request, std::nullopt});
}

void Session::finishWithRecommendation(RequestId request, const Recommendation& recommendation,
                                       bool hideoutReady) {
  JsonWriter payload;
  payload.beginObject()
      .key(payload::kFieldRecommendation).rawJson(recommendationToJson(recommendation))
      .key(payload::kFieldFromCache).value(recommendation.fromOfflineCache)
      .key(payload::kFieldHideoutMissing).value(!hideoutReady)
      .endObject();
  finishWithPayload(request, payload.takeText());
}

// ---------------------------------------------------------------------------
// Sign-in

bool Session::startSignIn(RequestId request, const uint8_t* signInEntropyBytes, size_t entropyLength) {
  std::lock_guard<std::mutex> lock(m_mutex);
  bool tooFewEntropyBytes = entropyLength < Authentication::kSignInEntropyBytes;
  if (tooFewEntropyBytes || isReservedForBridges(request) || !beginRequest(request)) {
    return false;
  }
  m_authentication.startSignIn(request, signInEntropyBytes);

  return true;
}

void Session::signInRedirect(std::string_view redirectUriOrEmptyIfClosed) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_authentication.handleRedirect(redirectUriOrEmptyIfClosed);
}

bool Session::signOut(RequestId request) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (isReservedForBridges(request) || !beginRequest(request)) {
    return false;
  }
  m_authentication.signOut(request);

  return true;
}

bool Session::signInEnabled() const { return m_authentication.enabled(); }

const std::optional<Account>& Session::currentAccount() const { return m_authentication.account(); }

void Session::startSignInRequestedByPage(BridgeId bridge) {
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeBridgeSignIn)
      .key(action::kFieldBridge).value(static_cast<int64_t>(bridge))
      .endObject();
  queueBridgeAction(bridge, json.takeText());
}

void Session::signOutRequestedByPage() { m_authentication.signOut(std::nullopt); }

void Session::storedSignInRestored() { m_favorites.switchToOwner(m_authentication.account()); }

void Session::notifyAccountChanged() {
  m_favorites.switchToOwner(m_authentication.account());

  const std::optional<Account>& account = m_authentication.account();
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeAccount)
      .key(action::kFieldSignedIn).value(account.has_value());
  if (account) {
    json.key(action::kFieldId).value(account->id).key(action::kFieldDisplayName).value(account->displayName);
  }
  json.endObject();
  m_actions.push_back(Action{json.takeText(), std::nullopt, std::nullopt, std::nullopt});

  for (auto& [id, bridge] : m_bridgesById) {
    bridge->onAccountChanged();
  }
}

// ---------------------------------------------------------------------------
// Favourites

bool Session::listFavorites(RequestId request) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (isReservedForBridges(request) || !beginRequest(request)) {
    return false;
  }
  m_favorites.listFavorites(request);
  m_favorites.syncIfChangesAreWaiting();

  return true;
}

bool Session::setFavorite(RequestId request, std::string_view itemId, bool favorite) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (isReservedForBridges(request) || !beginRequest(request)) {
    return false;
  }
  m_favorites.setFavorite(request, itemId, favorite);

  return true;
}

void Session::startFavoritesList(RequestId request) { m_favorites.listFavorites(request); }

void Session::startFavoriteSet(RequestId request, std::string_view itemId, bool favorite) {
  m_favorites.setFavorite(request, itemId, favorite);
}

std::optional<ItemNameAndIcon> Session::itemNameAndIcon(const std::string& itemId) {
  return m_engine.nameAndIcon(itemId);
}

void Session::notifyFavoritesChanged() {
  std::string items = m_favorites.favoritesJsonMostRecentFirst();
  JsonWriter json;
  json.beginObject().key(action::kFieldType).value(action::kTypeFavorites).endObject();
  m_actions.push_back(Action{json.takeText(), items, std::nullopt, std::nullopt});

  for (auto& [id, bridge] : m_bridgesById) {
    bridge->onFavoritesChanged(items);
  }
}

// ---------------------------------------------------------------------------
// Remote flags

// The cached document first, so a kill switch holds on a cold start without network.
void Session::loadRemoteFlags() {
  readCache(kRemoteFlagsCacheKey, [this](std::optional<std::string_view> storedValue) {
    if (auto cached = decodeCacheEntry(storedValue)) {
      if (auto flags = parseRemoteFlags(cached->body)) {
        applyRemoteFlags(std::move(*flags));
      }
    }

    HttpRequest flagsRequest{http::kMethodGet, m_configuration.remoteFlagsUrl, {}, std::nullopt};
    sendRequest(std::move(flagsRequest), [this](const HttpOutcome& response) {
      if (failureOfHttpResponse(response, kRemoteFlagsCacheKey)) {
        // Logged; the cached flags stay.
        return;
      }

      auto flags = parseRemoteFlags(response.body);
      if (!flags) {
        log(LogLevel::Warn, "ignoring malformed remote flags");
        return;
      }

      writeCache(kRemoteFlagsCacheKey, response.body);
      applyRemoteFlags(std::move(*flags));
    });
  });
}

// The platform needs only the kill switch's verdict, for a synchronous check.
void Session::applyRemoteFlags(RemoteFlags flags) {
  m_remoteFlags = std::move(flags);

  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeFlags)
      .key(action::kFieldLaunchAllowed).value(m_remoteFlags.killSwitchAllowsLaunch(TARKOV_COMPANION_VERSION_STRING))
      .endObject();
  m_actions.push_back(Action{json.takeText(), std::nullopt, std::nullopt, std::nullopt});
}

const RemoteFlags& Session::remoteFlags() const { return m_remoteFlags; }

// ---------------------------------------------------------------------------
// Actions

void Session::sendGraphqlRequest(std::string body, HttpContinuation then) {
  HttpRequest request{http::kMethodPost, m_configuration.endpoint, {{http::kHeaderContentType, http::kContentTypeJson}},
                      std::move(body)};
  sendRequest(std::move(request), std::move(then));
}

void Session::sendRequest(HttpRequest request, HttpContinuation then) {
  ActionId id = m_nextActionId++;
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeHttp)
      .key(action::kFieldId).value(static_cast<int64_t>(id))
      .key(action::kFieldMethod).value(request.method)
      .key(action::kFieldUrl).value(request.url)
      .key(action::kFieldHeaders).beginObject()
          .key(http::kHeaderAccept).value(http::kContentTypeJson);
  for (const auto& [name, value] : request.headersBesidesAcceptAndUserAgent) {
    json.key(name).value(value);
  }
  if (!m_configuration.userAgent.empty()) {
    json.key(http::kHeaderUserAgent).value(m_configuration.userAgent);
  }
  json.endObject()
      .key(action::kFieldTimeoutMilliseconds).value(kHttpTimeoutMilliseconds)
      .endObject();

  m_pendingActionsById[id].onHttp = std::move(then);
  m_actions.push_back(Action{json.takeText(), std::move(request.body), std::nullopt, std::nullopt});
}

void Session::readCache(const std::string& key, StoredValueContinuation then) {
  ActionId id = m_nextActionId++;
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeCacheRead)
      .key(action::kFieldId).value(static_cast<int64_t>(id))
      .key(action::kFieldKey).value(key)
      .endObject();

  m_pendingActionsById[id].onStoredValueRead = std::move(then);
  m_actions.push_back(Action{json.takeText(), std::nullopt, std::nullopt, std::nullopt});
}

void Session::readSecret(const std::string& key, StoredValueContinuation then) {
  ActionId id = m_nextActionId++;
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeSecretRead)
      .key(action::kFieldId).value(static_cast<int64_t>(id))
      .key(action::kFieldKey).value(key)
      .endObject();

  m_pendingActionsById[id].onStoredValueRead = std::move(then);
  m_actions.push_back(Action{json.takeText(), std::nullopt, std::nullopt, std::nullopt});
}

void Session::writeSecret(const std::string& key, std::string value) {
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeSecretWrite)
      .key(action::kFieldKey).value(key)
      .endObject();
  m_actions.push_back(Action{json.takeText(), std::move(value), std::nullopt, std::nullopt});
}

void Session::deleteSecret(const std::string& key) {
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeSecretDelete)
      .key(action::kFieldKey).value(key)
      .endObject();
  m_actions.push_back(Action{json.takeText(), std::nullopt, std::nullopt, std::nullopt});
}

void Session::readData(const std::string& key, StoredValueContinuation then) {
  ActionId id = m_nextActionId++;
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeDataRead)
      .key(action::kFieldId).value(static_cast<int64_t>(id))
      .key(action::kFieldKey).value(key)
      .endObject();

  m_pendingActionsById[id].onStoredValueRead = std::move(then);
  m_actions.push_back(Action{json.takeText(), std::nullopt, std::nullopt, std::nullopt});
}

void Session::writeData(const std::string& key, std::string value) {
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeDataWrite)
      .key(action::kFieldKey).value(key)
      .endObject();
  m_actions.push_back(Action{json.takeText(), std::move(value), std::nullopt, std::nullopt});
}

void Session::deleteData(const std::string& key) {
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeDataDelete)
      .key(action::kFieldKey).value(key)
      .endObject();
  m_actions.push_back(Action{json.takeText(), std::nullopt, std::nullopt, std::nullopt});
}

// Tagged with the request, so cancelling the sign-in drops a page not yet opened.
void Session::openBrowser(RequestId request, const std::string& url) {
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeOpenBrowser)
      .key(action::kFieldRequest).value(static_cast<int64_t>(request))
      .key(action::kFieldUrl).value(url)
      .endObject();
  m_actions.push_back(Action{json.takeText(), std::nullopt, request, std::nullopt});
}

// The value is "<epoch milliseconds>\n<response body>": the session stamps its
// own entries, so the platform's store needs no notion of time.
void Session::writeCache(const std::string& key, std::string_view body) {
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeCacheWrite)
      .key(action::kFieldKey).value(key)
      .endObject();

  std::string value = std::to_string(millisecondsSinceEpoch(m_wallClock()));
  value.push_back(kCacheEntryTimestampEnd);
  value.append(body);
  m_actions.push_back(Action{json.takeText(), std::move(value), std::nullopt, std::nullopt});
}

void Session::log(LogLevel level, std::string message) {
  JsonWriter json;
  json.beginObject()
      .key(action::kFieldType).value(action::kTypeLog)
      .key(action::kFieldLevel).value(logLevelName(level))
      .key(action::kFieldMessage).value(message)
      .endObject();
  m_actions.push_back(Action{json.takeText(), std::nullopt, std::nullopt, std::nullopt});
}

void Session::logUnknownKeys(const char* documentName, const std::vector<std::string>& keys) {
  for (const auto& key : keys) {
    log(LogLevel::Warn, std::string("ignoring unknown ") + documentName + " key '" + key + "'");
  }
}

// ---------------------------------------------------------------------------
// Helpers

// "<kind>|<game mode>|<language>[|<subject>]": prices and names differ per
// game mode and language, so they never share an entry.
std::string Session::cacheKey(std::string_view kind, std::string_view subject) const {
  std::string key(kind);
  key.append(kCacheKeySeparator).append(tarkovDevGameModeArgument(m_profile.gameMode));
  key.append(kCacheKeySeparator).append(m_configuration.language);
  if (!subject.empty()) {
    key.append(kCacheKeySeparator).append(subject);
  }

  return key;
}

// Opens {"query":..., "variables":{ with the variables every query takes
// (names as declared in contracts/queries/). The caller adds its own
// variables and closes both objects.
JsonWriter Session::beginGraphqlRequest(const char* query) const {
  JsonWriter body;
  body.beginObject()
      .key(tarkov_api::kRequestFieldQuery).value(query)
      .key(tarkov_api::kRequestFieldVariables).beginObject()
          .key(tarkov_api::kVariableLanguage).value(m_configuration.language)
          .key(tarkov_api::kVariableGameMode).value(tarkovDevGameModeArgument(m_profile.gameMode));

  return body;
}

std::string Session::catalogFileUrlOrEmpty(std::string_view pathInCatalog) const {
  const std::string& catalogBaseUrl = m_configuration.catalogBaseUrlWithoutTrailingSlash;
  if (catalogBaseUrl.empty()) {
    return {};
  }

  std::string url = catalogBaseUrl;
  url.append(http::kUrlPathSeparator).append(tarkovDevGameModeArgument(m_profile.gameMode));
  url.append(http::kUrlPathSeparator).append(catalog::catalogLanguageFor(m_configuration.language));

  return url.append(http::kUrlPathSeparator).append(pathInCatalog);
}

std::optional<Session::CacheEntry> Session::decodeCacheEntry(std::optional<std::string_view> storedValue) {
  if (!storedValue) {
    return std::nullopt;
  }

  size_t timestampEnd = storedValue->find(kCacheEntryTimestampEnd);
  std::string_view timestamp = storedValue->substr(0, timestampEnd);
  int64_t storedAtMilliseconds = 0;
  auto [parsedEnd, parseError] =
      std::from_chars(timestamp.data(), timestamp.data() + timestamp.size(), storedAtMilliseconds);
  bool corrupt = timestampEnd == std::string_view::npos || parseError != std::errc() ||
                 parsedEnd != timestamp.data() + timestamp.size() || storedAtMilliseconds < 0;
  if (corrupt) {
    log(LogLevel::Debug, "ignoring a corrupt offline cache entry");
    return std::nullopt;
  }

  return CacheEntry{SystemTime(milliseconds(storedAtMilliseconds)), std::string(storedValue->substr(timestampEnd + 1))};
}

// Data stamped in the future (the clock was moved back) is never fresh: its
// age is unknown, so it is refreshed, though still usable as a fallback.
bool Session::isFresh(SystemTime dataTime, milliseconds timeToLive) const {
  SystemTime now = m_wallClock();

  return dataTime <= now && now - dataTime < timeToLive;
}

}  // namespace tarkov_companion
