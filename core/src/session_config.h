// Session configuration and player profile. Keys and limits come from
// contracts/config-limits.json; these checks are authoritative.
#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "authentication.h"
#include "bridge.h"
#include "contract_generated.h"
#include "model.h"

namespace tarkov_companion {

struct SessionConfiguration {
  std::string endpoint = contract::kEndpointDefault;
  std::string language = contract::kLanguageDefault;
  std::string userAgent;
  size_t itemCacheCapacity = static_cast<size_t>(contract::kItemCacheCapacityDefault);
  std::chrono::seconds priceTimeToLive{contract::kPriceTimeToLiveSecondsDefault};
  std::string remoteFlagsUrl;
  SignInConfiguration signIn;
  // Set by the platform for debug builds of the host only, never anywhere else.
  bool developmentModeAllowsLocalHttp = false;
  std::string favoritesSyncUrl;
  std::string catalogBaseUrlWithoutTrailingSlash;
};

enum class GameMode { Regular, Pve };

const char* tarkovDevGameModeArgument(GameMode mode);

struct PlayerProfile {
  PlayerContext context;
  GameMode gameMode = GameMode::Regular;
};

template <typename Settings>
struct SettingsParseResult {
  Settings settings;
  std::string error;
  // Ignored for compatibility with newer platform layers, but logged.
  std::vector<std::string> unknownKeys;

  bool ok() const { return error.empty(); }
};

// Empty input means "all defaults".
SettingsParseResult<SessionConfiguration> parseSessionConfiguration(std::string_view json);
SettingsParseResult<PlayerProfile> parsePlayerProfile(std::string_view json);
SettingsParseResult<BridgeOptions> parseBridgeOptions(std::string_view json);

}  // namespace tarkov_companion
