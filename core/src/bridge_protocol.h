// Native side of the bridge protocol (docs/bridge-protocol.md). The page is
// untrusted: every message is size-capped, shape-checked and allow-listed.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "contract_generated.h"
#include "json.h"
#include "session_types.h"

namespace tarkov_companion::bridge {

inline constexpr int64_t kNativeProtocolVersion = 1;
inline constexpr int64_t kOldestServedWebProtocol = 1;
// Checked before parsing, so a huge message costs nothing.
inline constexpr size_t kMaximumMessageBytes = 16 * 1024;

inline constexpr const char* kRouteSearch = contract::bridge::kRouteSearch;
inline constexpr const char* kRouteItem = contract::bridge::kRouteItem;

bool isRoute(std::string_view route);

// Where the web flow is, so it can be reopened there after process death.
// Empty strings mean "not set".
struct UiState {
  std::string route = kRouteSearch;
  std::string query;
  std::string itemId;
};

using AnalyticsProperties = tarkov_companion::AnalyticsProperties;

enum class RequestType { Hello, Search, Recommend, Track, State, Close, SignIn, SignOut, FavoritesList, FavoriteSet };

struct Request {
  RequestType type = RequestType::Close;
  // Here rather than next to itemId, so it fills the gap after `type`.
  bool favoriteSetValue = false;
  std::string id;
  int64_t helloWebProtocol = 0;
  int64_t helloMinimumNativeProtocol = 0;
  std::string helloBundleVersion;
  std::string trimmedSearchQuery;
  std::string itemId;
  std::string trackEvent;
  AnalyticsProperties trackProperties;
  UiState reportedState;
};

struct ParsedMessage {
  std::optional<Request> validRequest;
  std::optional<std::string> idToAnswerWithError;
  std::string errorCode;
  std::string errorMessage;
};

ParsedMessage parseMessage(std::string_view rawMessage);

std::string okMessage(std::string_view id, std::string_view payloadJson);
std::string errorMessage(const std::optional<std::string>& id, std::string_view code, std::string_view message);
std::string eventMessage(std::string_view name, std::string_view payloadJson = kEmptyJsonObject);

}  // namespace tarkov_companion::bridge
