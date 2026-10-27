// Minimal JSON DOM and writer: exactly RFC 8259, depth-limited, no exceptions.
// A full library would dominate the binary size.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tarkov_companion {

// Deep enough for any GraphQL response we request, shallow enough that a
// malicious "[[[[..." can't exhaust the stack of a game's worker thread.
inline constexpr int kDefaultMaximumJsonDepth = 64;

class JsonValue {
 public:
  enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };
  using Member = std::pair<std::string, JsonValue>;

  JsonValue() = default;
  static JsonValue makeBool(bool value);
  static JsonValue makeNumber(double value);
  static JsonValue makeString(std::string value);
  static JsonValue makeArray();
  static JsonValue makeObject();

  Type type() const { return m_type; }
  bool isNull() const { return m_type == Type::Null; }
  bool isBool() const { return m_type == Type::Bool; }
  bool isNumber() const { return m_type == Type::Number; }
  bool isString() const { return m_type == Type::String; }
  bool isArray() const { return m_type == Type::Array; }
  bool isObject() const { return m_type == Type::Object; }

  bool asBool(bool fallback = false) const { return isBool() ? m_bool : fallback; }
  double asNumber(double fallback = 0) const { return isNumber() ? m_number : fallback; }
  int64_t asInteger(int64_t fallback = 0) const;
  std::string_view asString(std::string_view fallback = {}) const;

  // A value of another type reads as an empty list or a shared null, so
  // lookups can be chained without checks: value["data"]["items"].items().
  const std::vector<JsonValue>& items() const;
  const JsonValue& operator[](std::string_view key) const;
  const std::vector<Member>& members() const;
  bool hasKey(std::string_view key) const;

  std::vector<JsonValue>& mutableItems();
  std::vector<Member>& mutableMembers();

 private:
  double m_number = 0;
  std::string m_string;
  // Children live behind a pointer so an empty JsonValue stays small.
  std::shared_ptr<std::vector<JsonValue>> m_array;
  std::shared_ptr<std::vector<Member>> m_object;
  Type m_type = Type::Null;
  bool m_bool = false;
};

struct JsonParseResult {
  JsonValue value;
  bool ok = false;
  size_t errorOffset = 0;
  const char* error = nullptr;
};

inline constexpr const char* kJsonTrue = "true";
inline constexpr const char* kJsonFalse = "false";
inline constexpr const char* kJsonNull = "null";
inline constexpr const char* kEmptyJsonObject = "{}";

inline const char* jsonBooleanText(bool value) { return value ? kJsonTrue : kJsonFalse; }

JsonParseResult parseJson(std::string_view text, int maximumDepth = kDefaultMaximumJsonDepth);

class JsonWriter {
 public:
  JsonWriter& beginObject();
  JsonWriter& endObject();
  JsonWriter& beginArray();
  JsonWriter& endArray();
  JsonWriter& key(std::string_view name);
  JsonWriter& value(std::string_view text);
  JsonWriter& value(const char* text) { return value(std::string_view(text)); }
  JsonWriter& value(int64_t number);
  JsonWriter& value(int number) { return value(static_cast<int64_t>(number)); }
  JsonWriter& value(double number);
  JsonWriter& value(bool flag);
  JsonWriter& value(const JsonValue& parsedDocument);
  JsonWriter& null();
  // The caller vouches that `json` is valid: it comes from this writer or
  // from the recommender.
  JsonWriter& rawJson(std::string_view json);

  const std::string& text() const { return m_output; }
  std::string takeText() { return std::move(m_output); }

 private:
  void writeSeparator();

  std::string m_output;
  std::vector<bool> m_openContainerHasElement;
  bool m_justWroteKey = false;
};

void appendQuotedJsonString(std::string& output, std::string_view text);

}  // namespace tarkov_companion
