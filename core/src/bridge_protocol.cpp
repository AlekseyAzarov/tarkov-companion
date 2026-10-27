#include "bridge_protocol.h"

#include <algorithm>
#include <cmath>

#include "contract_generated.h"
#include "json.h"
#include "text.h"

namespace tarkov_companion::bridge {
namespace {

namespace wire = contract::bridge;

constexpr const char* kEnvelopeVersionField = wire::kFieldVersion;
constexpr const char* kEnvelopeIdField = wire::kFieldId;
constexpr const char* kEnvelopeReplyToField = wire::kFieldReplyTo;
constexpr const char* kEnvelopeTypeField = wire::kFieldType;
constexpr const char* kEnvelopePayloadField = wire::kFieldPayload;

constexpr size_t kRequestIdMaximumLength = 32;
constexpr size_t kBundleVersionMaximumLength = 32;
constexpr size_t kEventAndPropertyNameMaximumLength = 40;
constexpr size_t kMaximumTrackProperties = 10;
constexpr size_t kMaximumPropertyValueCharacters = 100;

// Validation without exceptions: each read fills `failure` and returns false,
// and parseMessage turns the first failure into an invalid_message.
struct PayloadReader {
  const JsonValue& payload;
  std::string failure;

  bool failBecauseInvalid(std::string fieldName) {
    failure = "invalid " + fieldName;

    return false;
  }

  bool readString(const char* key, std::string* output) {
    const JsonValue& value = payload[key];
    if (!value.isString()) {
      return failBecauseInvalid(key);
    }
    *output = std::string(value.asString());

    return true;
  }

  bool readStringLeavingEmptyIfAbsent(const char* key, std::string* output) {
    const JsonValue& value = payload[key];
    if (value.isNull()) {
      return true;
    }

    return readString(key, output);
  }

  bool readInteger(const char* key, int64_t* output) {
    const JsonValue& value = payload[key];
    if (!value.isNumber() || std::trunc(value.asNumber()) != value.asNumber()) {
      return failBecauseInvalid(key);
    }
    *output = value.asInteger();

    return true;
  }
};

bool isSnakeCaseName(std::string_view name, bool allowUppercaseAfterFirstLetter) {
  if (name.empty() || name.size() > kEventAndPropertyNameMaximumLength || !text::isLowercaseLetter(name.front())) {
    return false;
  }

  return std::all_of(name.begin() + 1, name.end(), [allowUppercaseAfterFirstLetter](char character) {
    return text::isLowercaseLetter(character) || text::isDigit(character) || character == '_' ||
           (allowUppercaseAfterFirstLetter && text::isUppercaseLetter(character));
  });
}

bool isBundleVersion(std::string_view version) {
  return text::hasLengthAndOnlyAllowedCharacters(version, 1, kBundleVersionMaximumLength, [](char character) {
    return text::isAlphanumeric(character) || character == '.' || character == '+' || character == '-';
  });
}

bool hasSearchQueryLength(std::string_view query) {
  size_t length = text::codePointCount(query);

  return length >= static_cast<size_t>(contract::kSearchQueryMinimumLength) &&
         length <= static_cast<size_t>(contract::kSearchQueryMaximumLength);
}

bool scalarPropertyValueAsText(const JsonValue& value, std::string* output) {
  if (value.isString()) {
    *output = std::string(value.asString());
  } else if (value.isBool()) {
    *output = jsonBooleanText(value.asBool());
  } else if (value.isNumber()) {
    double number = value.asNumber();
    bool isWholeNumber = std::trunc(number) == number;
    *output = isWholeNumber ? std::to_string(value.asInteger()) : JsonWriter().value(number).text();
  } else {
    return false;
  }

  return text::codePointCount(*output) <= kMaximumPropertyValueCharacters;
}

bool readHello(PayloadReader& reader, Request* request) {
  if (!reader.readInteger(wire::kFieldProtocol, &request->helloWebProtocol) ||
      !reader.readInteger(wire::kFieldMinimumNativeProtocol, &request->helloMinimumNativeProtocol) ||
      !reader.readString(wire::kFieldBundleVersion, &request->helloBundleVersion)) {
    return false;
  }

  return isBundleVersion(request->helloBundleVersion) || reader.failBecauseInvalid(wire::kFieldBundleVersion);
}

bool readSearch(PayloadReader& reader, Request* request) {
  std::string query;
  if (!reader.readString(wire::kFieldQuery, &query)) {
    return false;
  }
  request->trimmedSearchQuery = std::string(text::trimAsciiSpace(query));

  return hasSearchQueryLength(request->trimmedSearchQuery) || reader.failBecauseInvalid("query length");
}

bool readRecommend(PayloadReader& reader, Request* request) {
  if (!reader.readString(wire::kFieldItemId, &request->itemId)) {
    return false;
  }

  return text::isItemId(request->itemId) || reader.failBecauseInvalid(wire::kFieldItemId);
}

bool readFavoriteSet(PayloadReader& reader, Request* request) {
  if (!reader.readString(wire::kFieldItemId, &request->itemId)) {
    return false;
  }
  if (!text::isItemId(request->itemId)) {
    return reader.failBecauseInvalid(wire::kFieldItemId);
  }

  const JsonValue& favorite = reader.payload[wire::kFieldFavorite];
  if (!favorite.isBool()) {
    return reader.failBecauseInvalid(wire::kFieldFavorite);
  }
  request->favoriteSetValue = favorite.asBool();

  return true;
}

bool readTrack(PayloadReader& reader, Request* request) {
  if (!reader.readString(wire::kFieldEvent, &request->trackEvent)) {
    return false;
  }
  if (!isSnakeCaseName(request->trackEvent, false)) {
    return reader.failBecauseInvalid(wire::kFieldEvent);
  }

  const JsonValue& properties = reader.payload[wire::kFieldProperties];
  if (properties.isNull()) {
    return true;
  }
  if (!properties.isObject() || properties.members().size() > kMaximumTrackProperties) {
    return reader.failBecauseInvalid(wire::kFieldProperties);
  }

  for (const auto& [key, value] : properties.members()) {
    std::string valueText;
    if (!isSnakeCaseName(key, true)) {
      return reader.failBecauseInvalid("prop key");
    }
    if (!scalarPropertyValueAsText(value, &valueText)) {
      return reader.failBecauseInvalid("prop value");
    }
    request->trackProperties.emplace_back(key, std::move(valueText));
  }

  return true;
}

bool readState(PayloadReader& reader, Request* request) {
  UiState& state = request->reportedState;
  if (!reader.readString(wire::kFieldRoute, &state.route)) {
    return false;
  }
  if (!isRoute(state.route)) {
    return reader.failBecauseInvalid(wire::kFieldRoute);
  }

  if (!reader.readStringLeavingEmptyIfAbsent(wire::kFieldQuery, &state.query)) {
    return false;
  }
  if (text::codePointCount(state.query) > static_cast<size_t>(contract::kSearchQueryMaximumLength)) {
    return reader.failBecauseInvalid(wire::kFieldQuery);
  }

  if (!reader.readStringLeavingEmptyIfAbsent(wire::kFieldItemId, &state.itemId)) {
    return false;
  }

  return state.itemId.empty() || text::isItemId(state.itemId) || reader.failBecauseInvalid(wire::kFieldItemId);
}

ParsedMessage invalidMessage(std::optional<std::string> idToAnswer, const char* code, std::string message) {
  ParsedMessage parsed;
  parsed.idToAnswerWithError = std::move(idToAnswer);
  parsed.errorCode = code;
  parsed.errorMessage = std::move(message);

  return parsed;
}

}  // namespace

bool isRoute(std::string_view route) { return route == kRouteSearch || route == kRouteItem; }

ParsedMessage parseMessage(std::string_view rawMessage) {
  if (rawMessage.size() > kMaximumMessageBytes) {
    return invalidMessage(std::nullopt, contract::kBridgeErrorCodeInvalidMessage, "message too large");
  }

  JsonParseResult parsed = parseJson(rawMessage);
  if (!parsed.ok || !parsed.value.isObject()) {
    return invalidMessage(std::nullopt, contract::kBridgeErrorCodeInvalidMessage, "not a JSON object");
  }

  const JsonValue& envelope = parsed.value;
  std::string_view id = envelope[kEnvelopeIdField].asString();
  if (!envelope[kEnvelopeIdField].isString() || !text::isSafeToken(id, kRequestIdMaximumLength)) {
    return invalidMessage(std::nullopt, contract::kBridgeErrorCodeInvalidMessage, "bad id");
  }

  const JsonValue& version = envelope[kEnvelopeVersionField];
  if (!version.isNumber() || version.asNumber() < static_cast<double>(kOldestServedWebProtocol)) {
    return invalidMessage(std::string(id), contract::kBridgeErrorCodeInvalidMessage, "missing protocol version");
  }

  static const JsonValue kEmptyObject = JsonValue::makeObject();
  const JsonValue& payload =
      envelope[kEnvelopePayloadField].isObject() ? envelope[kEnvelopePayloadField] : kEmptyObject;
  PayloadReader reader{payload, {}};
  Request request;
  request.id = std::string(id);
  std::string_view type = envelope[kEnvelopeTypeField].asString();

  bool valid = false;
  if (type == wire::kRequestHello) {
    request.type = RequestType::Hello;
    valid = readHello(reader, &request);
  } else if (type == wire::kRequestSearch) {
    request.type = RequestType::Search;
    valid = readSearch(reader, &request);
  } else if (type == wire::kRequestRecommend) {
    request.type = RequestType::Recommend;
    valid = readRecommend(reader, &request);
  } else if (type == wire::kRequestTrack) {
    request.type = RequestType::Track;
    valid = readTrack(reader, &request);
  } else if (type == wire::kRequestState) {
    request.type = RequestType::State;
    valid = readState(reader, &request);
  } else if (type == wire::kRequestClose) {
    request.type = RequestType::Close;
    valid = true;
  } else if (type == wire::kRequestSignIn) {
    request.type = RequestType::SignIn;
    valid = true;
  } else if (type == wire::kRequestSignOut) {
    request.type = RequestType::SignOut;
    valid = true;
  } else if (type == wire::kRequestFavoritesList) {
    request.type = RequestType::FavoritesList;
    valid = true;
  } else if (type == wire::kRequestFavoriteSet) {
    request.type = RequestType::FavoriteSet;
    valid = readFavoriteSet(reader, &request);
  } else {
    // Newer web bundles may ask for things this SDK doesn't know. Answering
    // (instead of dropping) lets them degrade gracefully.
    std::string reason = "unsupported type '" + std::string(type) + "'";
    return invalidMessage(std::string(id), contract::kBridgeErrorCodeUnsupportedType, reason);
  }
  if (!valid) {
    return invalidMessage(std::string(id), contract::kBridgeErrorCodeInvalidMessage, reader.failure);
  }

  ParsedMessage result;
  result.validRequest = std::move(request);

  return result;
}

std::string okMessage(std::string_view id, std::string_view payloadJson) {
  JsonWriter message;
  message.beginObject()
      .key(kEnvelopeVersionField).value(kNativeProtocolVersion)
      .key(kEnvelopeReplyToField).value(id)
      .key(wire::kFieldOk).value(true)
      .key(kEnvelopePayloadField).rawJson(payloadJson)
      .endObject();

  return message.takeText();
}

std::string errorMessage(const std::optional<std::string>& id, std::string_view code, std::string_view message) {
  JsonWriter json;
  json.beginObject().key(kEnvelopeVersionField).value(kNativeProtocolVersion).key(kEnvelopeReplyToField);
  if (id) {
    json.value(*id);
  } else {
    json.null();
  }
  json.key(wire::kFieldOk).value(false)
      .key(wire::kFieldError).beginObject()
          .key(wire::kFieldCode).value(code)
          .key(wire::kFieldMessage).value(message)
      .endObject()
      .endObject();

  return json.takeText();
}

std::string eventMessage(std::string_view name, std::string_view payloadJson) {
  JsonWriter message;
  message.beginObject()
      .key(kEnvelopeVersionField).value(kNativeProtocolVersion)
      .key(wire::kFieldEvent).value(name)
      .key(kEnvelopePayloadField).rawJson(payloadJson)
      .endObject();

  return message.takeText();
}

}  // namespace tarkov_companion::bridge
