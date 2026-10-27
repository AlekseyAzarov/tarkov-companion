#include "bridge.h"

#include <algorithm>
#include <utility>

#include "contract_generated.h"
#include "http.h"
#include "json.h"

namespace tarkov_companion {
namespace {

using std::chrono::duration_cast;
using std::chrono::milliseconds;

namespace wire = contract::bridge;
namespace payload = contract::payload;

constexpr const char* kAlwaysOfferedFeatures[] = {wire::kFeatureSearch, wire::kFeatureRecommend,
                                                  wire::kFeatureTrack,  wire::kFeatureState,
                                                  wire::kFeatureClose,  wire::kFeatureIconProxy};
constexpr const char* kSignInFeatureOfferedWhenConfigured = wire::kFeatureSignIn;
constexpr const char* kFavoritesFeature = wire::kFeatureFavorites;
constexpr const char* kHandshakeResultAccepted = "ok";


}  // namespace

Bridge::Bridge(BridgeId id, BridgeOptions options, BridgeHost& host, SteadyClock clock, std::string sdkVersion)
    : m_clock(std::move(clock)),
      m_options(std::move(options)),
      m_openedAt(m_clock() - m_options.openedAgo),
      m_id(id),
      m_host(host),
      m_sdkVersion(std::move(sdkVersion)) {}

void Bridge::receive(std::string_view rawMessage) {
  if (m_silent) {
    return;
  }

  bridge::ParsedMessage parsed = bridge::parseMessage(rawMessage);
  if (!parsed.validRequest) {
    m_host.log(LogLevel::Warn, "bridge: rejected message (" + parsed.errorCode + ": " + parsed.errorMessage + ")");
    m_host.trackAnalyticsEvent(contract::kEventBridgeError, {{contract::kPropertyCode, parsed.errorCode}});
    send(bridge::errorMessage(parsed.idToAnswerWithError, parsed.errorCode, parsed.errorMessage));
    return;
  }

  handle(*parsed.validRequest);
}

void Bridge::sendEventOnceHandshakeIsDone(std::string_view name) {
  if (!m_silent && m_handshakeDone) {
    send(bridge::eventMessage(name));
  }
}

void Bridge::cancelEverythingAndGoSilent() {
  m_silent = true;
  for (const auto& [request, pending] : m_requestsInFlight) {
    m_host.cancelRequest(request);
  }
  m_requestsInFlight.clear();
  m_latestSearch.reset();
}

void Bridge::handle(const bridge::Request& request) {
  using bridge::RequestType;

  // Close works before the handshake: a bundle that failed it must still let the player leave.
  if (request.type != RequestType::Hello && request.type != RequestType::Close && !m_handshakeDone) {
    send(bridge::errorMessage(request.id, contract::kBridgeErrorCodeNotReady, "hello first"));
    return;
  }

  switch (request.type) {
    case RequestType::Hello:
      hello(request);
      break;
    case RequestType::Search:
      search(request);
      break;
    case RequestType::Recommend:
      recommend(request);
      break;
    case RequestType::Track:
      m_host.trackAnalyticsEvent(contract::kWebEventPrefix + request.trackEvent, request.trackProperties);
      send(bridge::okMessage(request.id, kEmptyJsonObject));
      break;
    case RequestType::State:
      m_host.saveState(m_id, request.reportedState);
      send(bridge::okMessage(request.id, kEmptyJsonObject));
      break;
    case RequestType::Close:
      m_host.closeScreen(m_id);
      break;
    case RequestType::SignIn:
    case RequestType::SignOut:
      startSignInOrSignOut(request);
      break;
    case RequestType::FavoritesList:
      registerThenStart(request, [this](RequestId started) { m_host.startFavoritesList(started); });
      break;
    case RequestType::FavoriteSet:
      registerThenStart(request, [this, &request](RequestId started) {
        m_host.startFavoriteSet(started, request.itemId, request.favoriteSetValue);
      });
      break;
  }
}

void Bridge::hello(const bridge::Request& request) {
  const RemoteFlags& flags = m_host.remoteFlags();
  int64_t oldestServed = std::max(bridge::kOldestServedWebProtocol, flags.minimumWebProtocol);

  std::optional<std::pair<const char*, std::string>> rejection;
  if (request.helloWebProtocol < oldestServed) {
    rejection.emplace(contract::kBridgeErrorCodeIncompatibleProtocol,
                      "web protocol " + std::to_string(request.helloWebProtocol) + " is no longer supported");
  } else if (request.helloMinimumNativeProtocol > bridge::kNativeProtocolVersion) {
    rejection.emplace(contract::kBridgeErrorCodeIncompatibleProtocol,
                      "web bundle needs native protocol " + std::to_string(request.helloMinimumNativeProtocol));
  } else if (flags.bundleDisabled(request.helloBundleVersion)) {
    rejection.emplace(contract::kBridgeErrorCodeBundleDisabled,
                      "bundle " + request.helloBundleVersion + " disabled remotely");
  }

  m_host.trackAnalyticsEvent(contract::kEventBridgeHandshake,
                             {{contract::kPropertyWebProtocol, std::to_string(request.helloWebProtocol)},
                              {contract::kPropertyBundleVersion, request.helloBundleVersion},
                              {contract::kPropertyResult, rejection ? rejection->first : kHandshakeResultAccepted}});
  if (rejection) {
    send(bridge::errorMessage(request.id, rejection->first, rejection->second));
    return;
  }

  m_handshakeDone = true;
  const bridge::UiState& state = m_options.initialState;
  JsonWriter payload;
  payload.beginObject()
      .key(wire::kFieldProtocol).value(bridge::kNativeProtocolVersion)
      .key(wire::kFieldSdkVersion).value(m_sdkVersion)
      .key(wire::kFieldFeatures).beginArray();
  for (const char* feature : kAlwaysOfferedFeatures) {
    payload.value(feature);
  }
  if (m_host.signInEnabled()) {
    payload.value(kSignInFeatureOfferedWhenConfigured);
  }
  payload.value(kFavoritesFeature);
  payload.endArray()
      .key(wire::kFieldIconProxy).value(std::string(contract::kIconProxyPath) + http::kUrlQueryStart +
                                        contract::kIconProxyUrlParameter + http::kUrlQueryValueStart)
      .key(wire::kFieldInitialState).beginObject().key(wire::kFieldRoute).value(state.route);
  if (!state.query.empty()) {
    payload.key(wire::kFieldQuery).value(state.query);
  }
  if (!state.itemId.empty()) {
    payload.key(wire::kFieldItemId).value(state.itemId);
  }
  payload.endObject();
  if (m_host.signInEnabled()) {
    payload.key(wire::kFieldAccount).rawJson(accountJsonVisibleToPage());
  }
  payload.endObject();

  send(bridge::okMessage(request.id, payload.text()));
}

// The page asks; the answer is only "started". The result reaches it as an
// account_changed event, like a sign-in the host started.
void Bridge::startSignInOrSignOut(const bridge::Request& request) {
  if (!m_host.signInEnabled()) {
    send(bridge::errorMessage(request.id, contract::kBridgeErrorCodeUnsupportedType, "sign-in is not configured"));
    return;
  }

  if (request.type == bridge::RequestType::SignIn) {
    m_host.startSignInRequestedByPage(m_id);
  } else {
    m_host.signOutRequestedByPage();
  }

  send(bridge::okMessage(request.id, kEmptyJsonObject));
}

void Bridge::onFavoritesChanged(const std::string& itemsJson) {
  if (m_silent || !m_handshakeDone) {
    return;
  }

  JsonWriter event;
  event.beginObject().key(wire::kFieldItems).rawJson(itemsJson).endObject();
  send(bridge::eventMessage(wire::kEventFavoritesChanged, event.takeText()));
}

void Bridge::onAccountChanged() {
  if (m_silent || !m_handshakeDone || !m_host.signInEnabled()) {
    return;
  }

  JsonWriter event;
  event.beginObject().key(wire::kFieldAccount).rawJson(accountJsonVisibleToPage()).endObject();
  send(bridge::eventMessage(wire::kEventAccountChanged, event.takeText()));
}

std::string Bridge::accountJsonVisibleToPage() const {
  const std::optional<Account>& account = m_host.currentAccount();
  JsonWriter json;
  json.beginObject().key(wire::kFieldSignedIn).value(account.has_value());
  if (account) {
    json.key(wire::kFieldDisplayName).value(account->displayName);
  }
  json.endObject();

  return json.takeText();
}

void Bridge::search(const bridge::Request& request) {
  if (m_latestSearch) {
    auto previous = m_requestsInFlight.find(*m_latestSearch);
    if (previous != m_requestsInFlight.end()) {
      std::string previousWebRequestId = previous->second.webRequestId;
      m_host.cancelRequest(previous->first);
      m_requestsInFlight.erase(previous);
      std::string reason = "superseded by " + request.id;
      send(bridge::errorMessage(previousWebRequestId, contract::kBridgeErrorCodeSuperseded, reason));
    }
  }

  registerThenStart(request, [this, &request](RequestId started) {
    m_latestSearch = started;
    m_host.startSearch(started, request.trimmedSearchQuery);
  });
}

void Bridge::recommend(const bridge::Request& request) {
  registerThenStart(request, [this, &request](RequestId started) { m_host.startRecommend(started, request.itemId); });
}

RequestId Bridge::registerThenStart(const bridge::Request& request, const std::function<void(RequestId)>& start) {
  RequestId started = m_host.newRequest(m_id);
  m_requestsInFlight[started] = PendingRequest{request.id, request.type, m_clock()};
  start(started);

  return started;
}

void Bridge::onSuccess(RequestId request, const std::string& sessionResultJson) {
  auto found = m_requestsInFlight.find(request);
  if (m_silent || found == m_requestsInFlight.end()) {
    return;
  }

  PendingRequest pending = std::move(found->second);
  m_requestsInFlight.erase(found);
  if (m_latestSearch == request) {
    m_latestSearch.reset();
  }

  JsonValue result = parseJson(sessionResultJson).value;
  std::string fromCache = jsonBooleanText(result[payload::kFieldFromCache].asBool());
  std::string latency = std::to_string(millisecondsSince(pending.startedAt));
  if (pending.type == bridge::RequestType::Search) {
    m_host.trackAnalyticsEvent(contract::kEventSearchCompleted,
                               {{contract::kPropertyLatencyMilliseconds, latency}, {contract::kPropertyFromCache, fromCache}});
  } else if (pending.type == bridge::RequestType::Recommend) {
    std::string verdict(result[payload::kFieldRecommendation][payload::kFieldVerdict].asString());
    bridge::AnalyticsProperties properties = {{contract::kPropertyVerdict, verdict},
                                              {contract::kPropertyLatencyMilliseconds, latency},
                                              {contract::kPropertyFromCache, fromCache}};
    // The product metric: how long from opening the overlay to the first answer.
    if (!m_firstVerdictTracked) {
      properties.emplace_back(contract::kPropertyTimeToFirstVerdictMilliseconds, std::to_string(millisecondsSince(m_openedAt)));
    }
    m_firstVerdictTracked = true;
    m_host.trackAnalyticsEvent(contract::kEventVerdictShown, std::move(properties));
  }

  send(bridge::okMessage(pending.webRequestId, sessionResultJson));
}

void Bridge::onFailure(RequestId request, const Failure& failure) {
  auto found = m_requestsInFlight.find(request);
  if (m_silent || found == m_requestsInFlight.end()) {
    return;
  }

  std::string webRequestId = std::move(found->second.webRequestId);
  m_requestsInFlight.erase(found);
  if (m_latestSearch == request) {
    m_latestSearch.reset();
  }

  m_host.trackAnalyticsEvent(contract::kEventBridgeRequestFailed, {{contract::kPropertyCode, errorCodeName(failure.code)}});
  send(bridge::errorMessage(webRequestId, errorCodeName(failure.code), failure.message));
}

void Bridge::send(std::string message) { m_host.sendToWeb(m_id, std::move(message)); }

int64_t Bridge::millisecondsSince(SteadyTime start) const {
  return static_cast<int64_t>(duration_cast<milliseconds>(m_clock() - start).count());
}

}  // namespace tarkov_companion
