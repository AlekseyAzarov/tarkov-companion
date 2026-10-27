// Remote flags and kill switch (docs/remote-flags.md).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tarkov_companion {

inline constexpr int64_t kSupportedFlagsSchema = 1;

struct RemoteFlags {
  bool webFlowEnabled = true;
  std::vector<std::string> disabledSdkVersions;
  std::vector<std::string> disabledBundleVersions;
  int64_t minimumWebProtocol = 0;

  bool killSwitchAllowsLaunch(std::string_view sdkVersion) const;
  bool bundleDisabled(std::string_view bundleVersion) const;
};

// Unknown fields are ignored and fields of the wrong type keep their defaults,
// so new flags never break old SDKs. A document with another schema, or that
// is not a JSON object, gives nullopt: a bad push must not brick the SDK.
std::optional<RemoteFlags> parseRemoteFlags(std::string_view document);

}  // namespace tarkov_companion
