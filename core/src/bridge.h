// One WebView connection's protocol state machine (ADR 0007). The session
// calls it with its lock held; it talks back only through BridgeHost.
#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "authentication.h"
#include "bridge_protocol.h"
#include "clock.h"
#include "remote_flags.h"
#include "session_types.h"

namespace tarkov_companion {

struct BridgeOptions {
  bridge::UiState initialState;
  // How long before the bridge opened the user asked for the overlay, so
  // time-to-first-verdict covers the whole wait.
  std::chrono::milliseconds openedAgo{0};
};

class BridgeHost {
 public:
  virtual void sendToWeb(BridgeId bridge, std::string message) = 0;
  virtual void closeScreen(BridgeId bridge) = 0;
  virtual void saveState(BridgeId bridge, const bridge::UiState& state) = 0;
  virtual void trackAnalyticsEvent(std::string event, bridge::AnalyticsProperties properties) = 0;
  virtual void log(LogLevel level, std::string message) = 0;
  // Work whose outcome comes back through Bridge::onSuccess/onFailure. The
  // bridge takes the id first and starts the work after it is ready for the
  // answer: the answer may come back before start*() returns.
  virtual RequestId newRequest(BridgeId bridge) = 0;
  virtual void startSearch(RequestId request, std::string_view query) = 0;
  virtual void startRecommend(RequestId request, std::string_view itemId) = 0;
  virtual void cancelRequest(RequestId request) = 0;
  // The flags in force now: read at handshake time, so flags that arrive
  // after the page opened still apply to it.
  virtual const RemoteFlags& remoteFlags() const = 0;
  // The page only ever learns whether the player is signed in and their
  // display name; tokens stay in the core.
  virtual bool signInEnabled() const = 0;
  virtual const std::optional<Account>& currentAccount() const = 0;
  virtual void startSignInRequestedByPage(BridgeId bridge) = 0;
  virtual void signOutRequestedByPage() = 0;
  virtual void startFavoritesList(RequestId request) = 0;
  virtual void startFavoriteSet(RequestId request, std::string_view itemId, bool favorite) = 0;

 protected:
  ~BridgeHost() = default;
};

class Bridge {
 public:
  Bridge(BridgeId id, BridgeOptions options, BridgeHost& host, SteadyClock clock, std::string sdkVersion);

  void receive(std::string_view rawMessage);
  void sendEventOnceHandshakeIsDone(std::string_view name);
  void cancelEverythingAndGoSilent();

  void onAccountChanged();
  void onFavoritesChanged(const std::string& itemsJson);

  void onSuccess(RequestId request, const std::string& sessionResultJson);
  void onFailure(RequestId request, const Failure& failure);

 private:
  struct PendingRequest {
    std::string webRequestId;
    bridge::RequestType type;
    SteadyTime startedAt;
  };

  void handle(const bridge::Request& request);
  void hello(const bridge::Request& request);
  void search(const bridge::Request& request);
  void recommend(const bridge::Request& request);
  // Registers the request, then lets `start` run it: in that order, so an
  // answer that comes back at once is not lost.
  RequestId registerThenStart(const bridge::Request& request, const std::function<void(RequestId)>& start);
  void startSignInOrSignOut(const bridge::Request& request);
  std::string accountJsonVisibleToPage() const;
  void send(std::string message);
  int64_t millisecondsSince(SteadyTime start) const;

  SteadyClock m_clock;
  const BridgeOptions m_options;
  const SteadyTime m_openedAt;
  // Typing produces a stream of searches; only the latest one matters.
  std::optional<RequestId> m_latestSearch;
  const BridgeId m_id;
  BridgeHost& m_host;
  const std::string m_sdkVersion;

  std::unordered_map<RequestId, PendingRequest> m_requestsInFlight;
  bool m_handshakeDone = false;
  bool m_firstVerdictTracked = false;
  bool m_silent = false;
};

}  // namespace tarkov_companion
