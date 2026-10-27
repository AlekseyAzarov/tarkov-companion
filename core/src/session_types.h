#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tarkov_companion {

using RequestId = uint64_t;
using ActionId = uint64_t;
using BridgeId = uint64_t;

enum class ErrorCode {
  InvalidArgument,
  Network,
  Api,
  SchemaMismatch,
  NotFound,
  Internal,
  SignInCancelled,
  SignInFailed,
  NotSignedIn,
};

const char* errorCodeName(ErrorCode code);

struct Failure {
  ErrorCode code = ErrorCode::Internal;
  std::string message;
};

enum class LogLevel { Debug, Info, Warn, Error };

struct HttpRequest {
  const char* method;
  std::string url;
  std::vector<std::pair<std::string, std::string>> headersBesidesAcceptAndUserAgent;
  std::optional<std::string> body;
};

struct HttpOutcome {
  static constexpr int kNoResponseStatusCodeWithReasonInBody = 0;

  int statusCode = kNoResponseStatusCodeWithReasonInBody;
  std::string_view body;
};

using HttpContinuation = std::function<void(const HttpOutcome&)>;
using StoredValueContinuation = std::function<void(std::optional<std::string_view>)>;

using AnalyticsProperties = std::vector<std::pair<std::string, std::string>>;

}  // namespace tarkov_companion
