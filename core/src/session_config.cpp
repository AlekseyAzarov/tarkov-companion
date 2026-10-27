#include "session_config.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>

#include "contract_generated.h"
#include "json.h"
#include "tarkov_api.h"
#include "text.h"

namespace tarkov_companion {
namespace {

constexpr int64_t kNoUpperLimit = std::numeric_limits<int32_t>::max();

template <size_t KeyCount>
std::vector<std::string> findUnknownKeys(const JsonValue& root, const char* const (&knownKeys)[KeyCount]) {
  std::vector<std::string> unknownKeys;
  for (const auto& member : root.members()) {
    bool known = std::any_of(std::begin(knownKeys), std::end(knownKeys),
                             [&](const char* knownKey) { return member.first == knownKey; });
    if (!known) {
      unknownKeys.push_back(member.first);
    }
  }

  return unknownKeys;
}

bool readIntegerIfPresent(const JsonValue& root, const char* key, int64_t minimum, int64_t maximum, int64_t* output,
                          std::string* error) {
  const JsonValue& value = root[key];
  if (value.isNull()) {
    return true;
  }

  double number = value.asNumber();
  // Fractions are rejected rather than rounded: 2.5 is a caller bug, not a 3.
  bool outOfRange = number < static_cast<double>(minimum) || number > static_cast<double>(maximum);
  if (!value.isNumber() || std::trunc(number) != number || outOfRange) {
    std::string allowed = maximum == kNoUpperLimit ? ">= " + std::to_string(minimum)
                                                   : "in " + std::to_string(minimum) + ".." + std::to_string(maximum);
    *error = std::string(key) + " must be an integer " + allowed;
    return false;
  }

  *output = static_cast<int64_t>(number);

  return true;
}

bool readBoolIfPresent(const JsonValue& root, const char* key, bool* output, std::string* error) {
  const JsonValue& value = root[key];
  if (value.isNull()) {
    return true;
  }
  if (!value.isBool()) {
    *error = std::string(key) + " must be a boolean";
    return false;
  }

  *output = value.asBool();

  return true;
}

bool readStringIfPresent(const JsonValue& root, const char* key, std::string* output, std::string* error) {
  const JsonValue& value = root[key];
  if (value.isNull()) {
    return true;
  }
  if (!value.isString()) {
    *error = std::string(key) + " must be a string";
    return false;
  }

  *output = std::string(value.asString());

  return true;
}

bool isLanguageCode(std::string_view code) {
  return code.size() == static_cast<size_t>(contract::kLanguageLength) &&
         std::all_of(code.begin(), code.end(), [](char letter) { return text::isLowercaseLetter(letter); });
}

// Loopback, or the host as the Android emulator sees it. Only allowed in
// development mode, which the platform sets for debug builds.
bool isHttpUrlOnThisMachine(std::string_view url) {
  for (std::string_view host : {"http://127.0.0.1", "http://localhost", "http://10.0.2.2"}) {
    if (!text::startsWith(url, host)) {
      continue;
    }

    std::string_view afterHost = url.substr(host.size());
    if (afterHost.empty() || afterHost.front() == ':' || afterHost.front() == '/') {
      return true;
    }
  }

  return false;
}

// "<scheme>:..." with an RFC 3986 scheme in lowercase: Android matches
// intent-filter schemes case-sensitively and browsers lowercase the scheme,
// so any other spelling never brings the player back.
bool hasLowercaseScheme(std::string_view uri) {
  size_t schemeEnd = uri.find(':');
  if (schemeEnd == std::string_view::npos || schemeEnd == 0 || !text::isLowercaseLetter(uri.front())) {
    return false;
  }

  std::string_view scheme = uri.substr(0, schemeEnd);

  return std::all_of(scheme.begin(), scheme.end(), [](char character) {
    return text::isLowercaseLetter(character) || text::isDigit(character) || character == '+' || character == '-' ||
           character == '.';
  });
}

void removeTrailingSlashes(std::string& url) {
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }
}

template <typename Settings>
bool parseRootTreatingEmptyAsObject(std::string_view json, SettingsParseResult<Settings>& result, JsonValue* root) {
  if (json.empty()) {
    *root = JsonValue::makeObject();
    return true;
  }

  JsonParseResult parsed = parseJson(json);
  if (!parsed.ok || !parsed.value.isObject()) {
    result.error = "settings must be a JSON object";
    return false;
  }

  *root = std::move(parsed.value);

  return true;
}

}  // namespace

const char* tarkovDevGameModeArgument(GameMode mode) {
  switch (mode) {
    case GameMode::Regular: return tarkov_api::kGameModeRegular;
    case GameMode::Pve: return tarkov_api::kGameModePve;
  }

  return tarkov_api::kGameModeRegular;
}

SettingsParseResult<SessionConfiguration> parseSessionConfiguration(std::string_view json) {
  SettingsParseResult<SessionConfiguration> result;
  JsonValue root;
  if (!parseRootTreatingEmptyAsObject(json, result, &root)) {
    return result;
  }

  SessionConfiguration& configuration = result.settings;
  std::string* error = &result.error;

  // First: it decides which URLs the rest of the document may use.
  if (!readBoolIfPresent(root, contract::kKeyDevelopmentMode, &configuration.developmentModeAllowsLocalHttp, error)) {
    return result;
  }
  auto urlIsAllowed = [&](const char* key, const std::string& url, const char* requiredPrefix) {
    bool localHttpAllowed = configuration.developmentModeAllowsLocalHttp && isHttpUrlOnThisMachine(url);
    if (url.empty() || text::startsWith(url, requiredPrefix) || localHttpAllowed) {
      return true;
    }

    *error = std::string(key) + " must start with " + requiredPrefix +
             (configuration.developmentModeAllowsLocalHttp ? " or be http on this machine" : "");

    return false;
  };

  if (!readStringIfPresent(root, contract::kKeyEndpoint, &configuration.endpoint, error)) {
    return result;
  }
  if (configuration.endpoint.empty()) {
    *error = std::string(contract::kKeyEndpoint) + " must not be empty";
    return result;
  }
  if (!urlIsAllowed(contract::kKeyEndpoint, configuration.endpoint, contract::kEndpointRequiredPrefix)) {
    return result;
  }

  if (!readStringIfPresent(root, contract::kKeyLanguage, &configuration.language, error)) {
    return result;
  }
  if (!isLanguageCode(configuration.language)) {
    *error = std::string(contract::kKeyLanguage) + " must be a lowercase " +
             std::to_string(contract::kLanguageLength) + "-letter code";
    return result;
  }

  if (!readStringIfPresent(root, contract::kKeyUserAgent, &configuration.userAgent, error)) {
    return result;
  }

  int64_t capacity = contract::kItemCacheCapacityDefault;
  if (!readIntegerIfPresent(root, contract::kKeyItemCacheCapacity, contract::kItemCacheCapacityMinimum,
                            contract::kItemCacheCapacityMaximum, &capacity, error)) {
    return result;
  }
  configuration.itemCacheCapacity = static_cast<size_t>(capacity);

  int64_t priceTimeToLiveSeconds = contract::kPriceTimeToLiveSecondsDefault;
  if (!readIntegerIfPresent(root, contract::kKeyPriceTimeToLiveSeconds, contract::kPriceTimeToLiveSecondsMinimum, kNoUpperLimit,
                            &priceTimeToLiveSeconds, error)) {
    return result;
  }
  configuration.priceTimeToLive = std::chrono::seconds(priceTimeToLiveSeconds);

  if (!readStringIfPresent(root, contract::kKeyRemoteFlagsUrl, &configuration.remoteFlagsUrl, error)) {
    return result;
  }
  if (!urlIsAllowed(contract::kKeyRemoteFlagsUrl, configuration.remoteFlagsUrl, contract::kRemoteFlagsUrlRequiredPrefix)) {
    return result;
  }

  SignInConfiguration& signIn = configuration.signIn;
  if (!readStringIfPresent(root, contract::kKeySignInIssuer, &signIn.issuer, error) ||
      !readStringIfPresent(root, contract::kKeySignInClientId, &signIn.clientId, error) ||
      !readStringIfPresent(root, contract::kKeySignInRedirectUri, &signIn.redirectUri, error)) {
    return result;
  }
  if (!urlIsAllowed(contract::kKeySignInIssuer, signIn.issuer, contract::kSignInIssuerRequiredPrefix)) {
    return result;
  }
  removeTrailingSlashes(signIn.issuer);
  if (!signIn.issuer.empty() && (signIn.clientId.empty() || !hasLowercaseScheme(signIn.redirectUri))) {
    *error = std::string(contract::kKeySignInIssuer) + " needs " + contract::kKeySignInClientId + " and an " +
             contract::kKeySignInRedirectUri + " with a lowercase scheme";
    return result;
  }

  if (!readStringIfPresent(root, contract::kKeyFavoritesSyncUrl, &configuration.favoritesSyncUrl, error)) {
    return result;
  }
  if (!urlIsAllowed(contract::kKeyFavoritesSyncUrl, configuration.favoritesSyncUrl,
                    contract::kFavoritesSyncUrlRequiredPrefix)) {
    return result;
  }

  std::string& catalogUrl = configuration.catalogBaseUrlWithoutTrailingSlash;
  if (!readStringIfPresent(root, contract::kKeyCatalogUrl, &catalogUrl, error)) {
    return result;
  }
  if (!urlIsAllowed(contract::kKeyCatalogUrl, catalogUrl, contract::kCatalogUrlRequiredPrefix)) {
    return result;
  }
  removeTrailingSlashes(catalogUrl);

  result.unknownKeys = findUnknownKeys(root, contract::kConfigKeys);

  return result;
}

SettingsParseResult<PlayerProfile> parsePlayerProfile(std::string_view json) {
  SettingsParseResult<PlayerProfile> result;
  JsonValue root;
  if (!parseRootTreatingEmptyAsObject(json, result, &root)) {
    return result;
  }

  PlayerContext& context = result.settings.context;
  std::string* error = &result.error;

  int64_t level = contract::kPlayerLevelDefault;
  if (!readIntegerIfPresent(root, contract::kKeyPlayerLevel, contract::kPlayerLevelMinimum, contract::kPlayerLevelMaximum,
                            &level, error)) {
    return result;
  }
  context.playerLevel = static_cast<int>(level);

  // Unless the platform says otherwise, the flea follows the player level.
  context.fleaUnlocked = level >= contract::kFleaUnlockLevel;
  if (!readBoolIfPresent(root, contract::kKeyFleaUnlocked, &context.fleaUnlocked, error)) {
    return result;
  }

  int64_t intelligenceCenterLevel = contract::kIntelligenceCenterLevelDefault;
  if (!readIntegerIfPresent(root, contract::kKeyIntelligenceCenterLevel, contract::kIntelligenceCenterLevelMinimum,
                            contract::kIntelligenceCenterLevelMaximum, &intelligenceCenterLevel, error)) {
    return result;
  }
  context.intelligenceCenterLevel = static_cast<int>(intelligenceCenterLevel);

  std::string gameMode = contract::kGameModeDefault;
  if (!readStringIfPresent(root, contract::kKeyGameMode, &gameMode, error)) {
    return result;
  }
  if (gameMode == tarkovDevGameModeArgument(GameMode::Regular)) {
    result.settings.gameMode = GameMode::Regular;
  } else if (gameMode == tarkovDevGameModeArgument(GameMode::Pve)) {
    result.settings.gameMode = GameMode::Pve;
  } else {
    *error = std::string(contract::kKeyGameMode) + " must be \"" + tarkov_api::kGameModeRegular + "\" or \"" +
             tarkov_api::kGameModePve + "\"";
    return result;
  }

  if (!readBoolIfPresent(root, contract::kKeyIgnoreKeepReasons, &context.ignoreKeepReasons, error)) {
    return result;
  }

  int64_t marginPercent = contract::kFleaMarginPercentDefault;
  if (!readIntegerIfPresent(root, contract::kKeyFleaMarginPercent, contract::kFleaMarginPercentMinimum,
                            contract::kFleaMarginPercentMaximum, &marginPercent, error)) {
    return result;
  }
  context.fleaMargin = static_cast<double>(marginPercent) / kPercentInWhole;

  result.unknownKeys = findUnknownKeys(root, contract::kProfileKeys);

  return result;
}

SettingsParseResult<BridgeOptions> parseBridgeOptions(std::string_view json) {
  SettingsParseResult<BridgeOptions> result;
  JsonValue root;
  if (!parseRootTreatingEmptyAsObject(json, result, &root)) {
    return result;
  }

  BridgeOptions& options = result.settings;
  std::string* error = &result.error;

  const JsonValue& state = root[contract::kKeyInitialState];
  if (!state.isNull()) {
    bridge::UiState& initial = options.initialState;
    bool valid = state.isObject() && readStringIfPresent(state, contract::bridge::kFieldRoute, &initial.route, error) &&
                 readStringIfPresent(state, contract::bridge::kFieldQuery, &initial.query, error) &&
                 readStringIfPresent(state, contract::bridge::kFieldItemId, &initial.itemId, error) &&
                 bridge::isRoute(initial.route) &&
                 text::codePointCount(initial.query) <= static_cast<size_t>(contract::kSearchQueryMaximumLength) &&
                 (initial.itemId.empty() || text::isItemId(initial.itemId));
    if (!valid) {
      *error = std::string(contract::kKeyInitialState) + " must be {route: search|item, query?, itemId?}";
      return result;
    }
  }

  int64_t openedMillisecondsAgo = contract::kOpenedMillisecondsAgoDefault;
  if (!readIntegerIfPresent(root, contract::kKeyOpenedMillisecondsAgo, contract::kOpenedMillisecondsAgoMinimum, kNoUpperLimit,
                            &openedMillisecondsAgo, error)) {
    return result;
  }
  options.openedAgo = std::chrono::milliseconds(openedMillisecondsAgo);

  result.unknownKeys = findUnknownKeys(root, contract::kBridgeOptionsKeys);

  return result;
}

}  // namespace tarkov_companion
