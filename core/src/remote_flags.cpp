#include "remote_flags.h"

#include <algorithm>

#include "json.h"

namespace tarkov_companion {
namespace {

constexpr const char* kFieldSchema = "schema";
constexpr const char* kFieldWebFlowEnabled = "webFlowEnabled";
constexpr const char* kFieldDisabledSdkVersions = "disabledSdkVersions";
constexpr const char* kFieldDisabledBundleVersions = "disabledBundleVersions";
constexpr const char* kFieldMinimumWebProtocol = "minimumWebProtocol";

bool contains(const std::vector<std::string>& values, std::string_view value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

std::vector<std::string> stringItemsIgnoringOthers(const JsonValue& array) {
  std::vector<std::string> values;
  for (const JsonValue& item : array.items()) {
    if (item.isString()) {
      values.emplace_back(item.asString());
    }
  }

  return values;
}

}  // namespace

bool RemoteFlags::killSwitchAllowsLaunch(std::string_view sdkVersion) const {
  return webFlowEnabled && !contains(disabledSdkVersions, sdkVersion);
}

bool RemoteFlags::bundleDisabled(std::string_view bundleVersion) const {
  return contains(disabledBundleVersions, bundleVersion);
}

std::optional<RemoteFlags> parseRemoteFlags(std::string_view document) {
  JsonParseResult parsed = parseJson(document);
  if (!parsed.ok || !parsed.value.isObject()) {
    return std::nullopt;
  }

  const JsonValue& root = parsed.value;
  if (root[kFieldSchema].asInteger() != kSupportedFlagsSchema) {
    return std::nullopt;
  }

  RemoteFlags flags;
  flags.webFlowEnabled = root[kFieldWebFlowEnabled].asBool(true);
  flags.disabledSdkVersions = stringItemsIgnoringOthers(root[kFieldDisabledSdkVersions]);
  flags.disabledBundleVersions = stringItemsIgnoringOthers(root[kFieldDisabledBundleVersions]);
  flags.minimumWebProtocol = root[kFieldMinimumWebProtocol].asInteger(0);

  return flags;
}

}  // namespace tarkov_companion
