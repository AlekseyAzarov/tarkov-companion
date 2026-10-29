#include "json.h"

#include <cmath>
#include <cstdio>
#include <optional>

#include "text.h"
#include "utf8.h"

namespace tarkov_companion {
namespace {

// UTF-16 surrogate ranges (RFC 8259 §7: characters outside the BMP are
// escaped as a high/low surrogate pair).
constexpr uint32_t kHighSurrogateFirst = 0xD800;
constexpr uint32_t kHighSurrogateLast = 0xDBFF;
constexpr uint32_t kLowSurrogateFirst = 0xDC00;
constexpr uint32_t kLowSurrogateLast = 0xDFFF;
constexpr uint32_t kFirstSupplementaryCodePoint = 0x10000;
constexpr int kSurrogatePayloadBits = 10;

constexpr std::string_view kUnicodeEscapePrefix = "\\u";
constexpr int kHexDigitsInUnicodeEscape = 4;
constexpr int kBitsPerHexDigit = 4;
constexpr unsigned char kFirstCharacterNotNeedingEscape = 0x20;

// Exponents beyond this overflow/underflow a double anyway; stop accumulating
// digits so a hostile "1e99999999999" can't overflow the int.
constexpr int kExponentLimit = 10000;

// A double holds about 17 significant digits. Digits past this limit are
// dropped (integer ones still scale the value), so a long number can't push
// the mantissa to infinity.
constexpr int kSignificantDigitLimit = 19;

// Just under INT64_MAX (about 9.22e18): a double beyond it can't be cast to
// int64_t. Above 2^53 doubles are whole but no longer exact; the cast stays safe.
constexpr double kSafeInt64CastLimit = 9.0e18;

// "%.6g": enough for every double the core writes (ratios, not prices).
constexpr const char* kDoubleFormat = "%.6g";

const JsonValue& sharedNull() {
  static const JsonValue kNull;

  return kNull;
}

const std::vector<JsonValue>& sharedEmptyItems() {
  static const std::vector<JsonValue> kEmpty;

  return kEmpty;
}

const std::vector<JsonValue::Member>& sharedEmptyMembers() {
  static const std::vector<JsonValue::Member> kEmpty;

  return kEmpty;
}

class Parser {
 public:
  Parser(std::string_view text, int maximumDepth) : m_text(text), m_maximumDepth(maximumDepth) {}

  JsonParseResult run() {
    JsonParseResult result;
    skipWhitespace();
    if (!parseValue(result.value, 0)) {
      return failed(result);
    }

    skipWhitespace();
    if (m_position != m_text.size()) {
      m_error = "trailing characters";
      return failed(result);
    }

    result.ok = true;

    return result;
  }

 private:
  JsonParseResult& failed(JsonParseResult& result) {
    result.ok = false;
    result.value = JsonValue();
    result.error = m_error ? m_error : "invalid json";
    result.errorOffset = m_position;

    return result;
  }

  bool isAtEnd() const { return m_position >= m_text.size(); }
  char currentCharacter() const { return isAtEnd() ? '\0' : m_text[m_position]; }

  void skipWhitespace() {
    while (!isAtEnd()) {
      char current = m_text[m_position];
      if (current != ' ' && current != '\t' && current != '\n' && current != '\r') {
        break;
      }
      ++m_position;
    }
  }

  bool consumeLiteral(std::string_view literal) {
    if (m_text.substr(m_position, literal.size()) != literal) {
      m_error = "invalid literal";
      return false;
    }
    m_position += literal.size();

    return true;
  }

  bool parseValue(JsonValue& output, int depth) {
    if (depth > m_maximumDepth) {
      m_error = "nesting too deep";
      return false;
    }

    switch (currentCharacter()) {
      case '{': return parseObject(output, depth);
      case '[': return parseArray(output, depth);
      case '"': return parseStringValue(output);
      case 't': return parseLiteralValue(kJsonTrue, JsonValue::makeBool(true), output);
      case 'f': return parseLiteralValue(kJsonFalse, JsonValue::makeBool(false), output);
      case 'n': return parseLiteralValue(kJsonNull, JsonValue(), output);
      default: return parseNumber(output);
    }
  }

  bool parseStringValue(JsonValue& output) {
    std::string text;
    if (!parseString(text)) {
      return false;
    }
    output = JsonValue::makeString(std::move(text));

    return true;
  }

  bool parseLiteralValue(std::string_view literal, JsonValue value, JsonValue& output) {
    if (!consumeLiteral(literal)) {
      return false;
    }
    output = std::move(value);

    return true;
  }

  bool parseObject(JsonValue& output, int depth) {
    ++m_position;
    output = JsonValue::makeObject();
    auto& members = output.mutableMembers();

    skipWhitespace();
    if (currentCharacter() == '}') {
      ++m_position;
      return true;
    }

    while (true) {
      skipWhitespace();
      if (currentCharacter() != '"') {
        m_error = "expected object key";
        return false;
      }
      std::string key;
      if (!parseString(key)) {
        return false;
      }

      skipWhitespace();
      if (currentCharacter() != ':') {
        m_error = "expected ':'";
        return false;
      }
      ++m_position;

      skipWhitespace();
      JsonValue child;
      if (!parseValue(child, depth + 1)) {
        return false;
      }
      members.emplace_back(std::move(key), std::move(child));

      skipWhitespace();
      if (currentCharacter() == ',') {
        ++m_position;
        continue;
      }
      if (currentCharacter() == '}') {
        ++m_position;
        return true;
      }

      m_error = "expected ',' or '}'";
      return false;
    }
  }

  bool parseArray(JsonValue& output, int depth) {
    ++m_position;
    output = JsonValue::makeArray();
    auto& items = output.mutableItems();

    skipWhitespace();
    if (currentCharacter() == ']') {
      ++m_position;
      return true;
    }

    while (true) {
      skipWhitespace();
      JsonValue child;
      if (!parseValue(child, depth + 1)) {
        return false;
      }
      items.push_back(std::move(child));

      skipWhitespace();
      if (currentCharacter() == ',') {
        ++m_position;
        continue;
      }
      if (currentCharacter() == ']') {
        ++m_position;
        return true;
      }

      m_error = "expected ',' or ']'";
      return false;
    }
  }

  bool parseFourHexDigits(uint32_t& output) {
    if (m_position + kHexDigitsInUnicodeEscape > m_text.size()) {
      m_error = "truncated unicode escape";
      return false;
    }

    output = 0;
    for (int digitIndex = 0; digitIndex < kHexDigitsInUnicodeEscape; ++digitIndex) {
      std::optional<int> digit = text::hexDigitValue(m_text[m_position + digitIndex]);
      if (!digit) {
        m_error = "invalid unicode escape";
        return false;
      }
      output = (output << kBitsPerHexDigit) | static_cast<uint32_t>(*digit);
    }
    m_position += kHexDigitsInUnicodeEscape;

    return true;
  }

  bool parseUnicodeEscapeJoiningSurrogatePairs(std::string& output) {
    uint32_t codePoint;
    if (!parseFourHexDigits(codePoint)) {
      return false;
    }

    if (codePoint >= kHighSurrogateFirst && codePoint <= kHighSurrogateLast) {
      if (m_text.substr(m_position, kUnicodeEscapePrefix.size()) != kUnicodeEscapePrefix) {
        m_error = "unpaired surrogate";
        return false;
      }
      m_position += kUnicodeEscapePrefix.size();

      uint32_t lowSurrogate;
      if (!parseFourHexDigits(lowSurrogate)) {
        return false;
      }
      if (lowSurrogate < kLowSurrogateFirst || lowSurrogate > kLowSurrogateLast) {
        m_error = "invalid low surrogate";
        return false;
      }

      codePoint = kFirstSupplementaryCodePoint + ((codePoint - kHighSurrogateFirst) << kSurrogatePayloadBits) +
                  (lowSurrogate - kLowSurrogateFirst);
    } else if (codePoint >= kLowSurrogateFirst && codePoint <= kLowSurrogateLast) {
      m_error = "unpaired surrogate";
      return false;
    }

    utf8::append(output, codePoint);

    return true;
  }

  bool parseString(std::string& output) {
    ++m_position;

    while (true) {
      if (isAtEnd()) {
        m_error = "unterminated string";
        return false;
      }

      char current = m_text[m_position++];
      if (current == '"') {
        return true;
      }
      if (static_cast<unsigned char>(current) < kFirstCharacterNotNeedingEscape) {
        m_error = "control character in string";
        return false;
      }
      if (current != '\\') {
        output.push_back(current);
        continue;
      }

      if (isAtEnd()) {
        m_error = "unterminated escape";
        return false;
      }
      if (!parseEscapedCharacter(m_text[m_position++], output)) {
        return false;
      }
    }
  }

  bool parseEscapedCharacter(char escaped, std::string& output) {
    switch (escaped) {
      case '"': output.push_back('"'); return true;
      case '\\': output.push_back('\\'); return true;
      case '/': output.push_back('/'); return true;
      case 'b': output.push_back('\b'); return true;
      case 'f': output.push_back('\f'); return true;
      case 'n': output.push_back('\n'); return true;
      case 'r': output.push_back('\r'); return true;
      case 't': output.push_back('\t'); return true;
      case 'u': return parseUnicodeEscapeJoiningSurrogatePairs(output);
      default: m_error = "invalid escape"; return false;
    }
  }

  // Locale-independent number parsing (strtod honours LC_NUMERIC, which a
  // host app may have changed).
  bool parseNumber(JsonValue& output) {
    size_t start = m_position;
    bool negative = false;
    if (currentCharacter() == '-') {
      negative = true;
      ++m_position;
    }
    if (!text::isDigit(currentCharacter())) {
      m_position = start;
      m_error = "unexpected character";
      return false;
    }

    double mantissa = 0;
    int significantDigits = 0;
    int decimalShift = 0;
    auto appendToMantissa = [&](char digit) {
      if (significantDigits == kSignificantDigitLimit) {
        return false;
      }
      mantissa = mantissa * 10 + (digit - '0');
      if (mantissa != 0) {
        ++significantDigits;
      }

      return true;
    };

    if (currentCharacter() == '0') {
      ++m_position;  // a leading zero can't be followed by more digits
    } else {
      while (text::isDigit(currentCharacter())) {
        if (!appendToMantissa(m_text[m_position++])) {
          ++decimalShift;
        }
      }
    }

    if (currentCharacter() == '.') {
      ++m_position;
      if (!text::isDigit(currentCharacter())) {
        m_error = "expected digit after '.'";
        return false;
      }
      while (text::isDigit(currentCharacter())) {
        if (appendToMantissa(m_text[m_position++])) {
          --decimalShift;
        }
      }
    }

    int exponent = 0;
    if (currentCharacter() == 'e' || currentCharacter() == 'E') {
      if (!parseExponent(exponent)) {
        return false;
      }
    }

    // A zero mantissa stays zero: "0e400" would otherwise be 0 * inf, a NaN.
    double magnitude = mantissa == 0 ? 0 : mantissa * std::pow(10.0, exponent + decimalShift);
    output = JsonValue::makeNumber(negative ? -magnitude : magnitude);

    return true;
  }

  bool parseExponent(int& exponent) {
    ++m_position;
    bool negativeExponent = false;
    if (currentCharacter() == '+' || currentCharacter() == '-') {
      negativeExponent = m_text[m_position++] == '-';
    }
    if (!text::isDigit(currentCharacter())) {
      m_error = "expected exponent digits";
      return false;
    }

    while (text::isDigit(currentCharacter())) {
      int digit = m_text[m_position++] - '0';
      if (exponent < kExponentLimit) {
        exponent = exponent * 10 + digit;
      }
    }
    if (negativeExponent) {
      exponent = -exponent;
    }

    return true;
  }

  std::string_view m_text;
  size_t m_position = 0;
  int m_maximumDepth;
  const char* m_error = nullptr;
};

}  // namespace

JsonValue JsonValue::makeBool(bool value) {
  JsonValue result;
  result.m_type = Type::Bool;
  result.m_bool = value;

  return result;
}

JsonValue JsonValue::makeNumber(double value) {
  JsonValue result;
  result.m_type = Type::Number;
  result.m_number = value;

  return result;
}

JsonValue JsonValue::makeString(std::string value) {
  JsonValue result;
  result.m_type = Type::String;
  result.m_string = std::move(value);

  return result;
}

JsonValue JsonValue::makeArray() {
  JsonValue result;
  result.m_type = Type::Array;
  result.m_array = std::make_shared<std::vector<JsonValue>>();

  return result;
}

JsonValue JsonValue::makeObject() {
  JsonValue result;
  result.m_type = Type::Object;
  result.m_object = std::make_shared<std::vector<Member>>();

  return result;
}

int64_t JsonValue::asInteger(int64_t fallback) const {
  if (!isNumber() || !std::isfinite(m_number)) {
    return fallback;
  }
  if (m_number > kSafeInt64CastLimit || m_number < -kSafeInt64CastLimit) {
    return fallback;
  }

  return static_cast<int64_t>(std::llround(m_number));
}

std::string_view JsonValue::asString(std::string_view fallback) const {
  return isString() ? std::string_view(m_string) : fallback;
}

const std::vector<JsonValue>& JsonValue::items() const { return isArray() ? *m_array : sharedEmptyItems(); }

const std::vector<JsonValue::Member>& JsonValue::members() const {
  return isObject() ? *m_object : sharedEmptyMembers();
}

const JsonValue& JsonValue::operator[](std::string_view key) const {
  if (!isObject()) {
    return sharedNull();
  }

  // Linear scan: GraphQL objects here have < 20 keys, which beats hashing.
  for (const auto& member : *m_object) {
    if (member.first == key) {
      return member.second;
    }
  }

  return sharedNull();
}

bool JsonValue::hasKey(std::string_view key) const {
  if (!isObject()) {
    return false;
  }

  for (const auto& member : *m_object) {
    if (member.first == key) {
      return true;
    }
  }

  return false;
}

std::vector<JsonValue>& JsonValue::mutableItems() { return *m_array; }
std::vector<JsonValue::Member>& JsonValue::mutableMembers() { return *m_object; }

JsonParseResult parseJson(std::string_view text, int maximumDepth) { return Parser(text, maximumDepth).run(); }

void appendQuotedJsonString(std::string& output, std::string_view text) {
  output.push_back('"');
  for (char character : text) {
    switch (character) {
      case '"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      case '\b': output += "\\b"; break;
      case '\f': output += "\\f"; break;
      default:
        if (static_cast<unsigned char>(character) < kFirstCharacterNotNeedingEscape) {
          char unicodeEscape[sizeof("\\u0000")];
          std::snprintf(unicodeEscape, sizeof(unicodeEscape), "\\u%04x", static_cast<unsigned>(character));
          output += unicodeEscape;
        } else {
          output.push_back(character);
        }
    }
  }
  output.push_back('"');
}

void JsonWriter::writeSeparator() {
  if (m_justWroteKey) {
    m_justWroteKey = false;
    return;
  }

  if (!m_openContainerHasElement.empty()) {
    if (m_openContainerHasElement.back()) {
      m_output.push_back(',');
    }
    m_openContainerHasElement.back() = true;
  }
}

JsonWriter& JsonWriter::beginObject() {
  writeSeparator();
  m_output.push_back('{');
  m_openContainerHasElement.push_back(false);

  return *this;
}

JsonWriter& JsonWriter::endObject() {
  m_output.push_back('}');
  m_openContainerHasElement.pop_back();

  return *this;
}

JsonWriter& JsonWriter::beginArray() {
  writeSeparator();
  m_output.push_back('[');
  m_openContainerHasElement.push_back(false);

  return *this;
}

JsonWriter& JsonWriter::endArray() {
  m_output.push_back(']');
  m_openContainerHasElement.pop_back();

  return *this;
}

JsonWriter& JsonWriter::key(std::string_view name) {
  writeSeparator();
  appendQuotedJsonString(m_output, name);
  m_output.push_back(':');
  m_justWroteKey = true;

  return *this;
}

JsonWriter& JsonWriter::value(std::string_view text) {
  writeSeparator();
  appendQuotedJsonString(m_output, text);

  return *this;
}

JsonWriter& JsonWriter::value(int64_t number) {
  writeSeparator();
  m_output += std::to_string(number);

  return *this;
}

JsonWriter& JsonWriter::value(double number) {
  writeSeparator();
  if (!std::isfinite(number)) {
    m_output += kJsonNull;
    return *this;
  }

  char formatted[32];
  std::snprintf(formatted, sizeof(formatted), kDoubleFormat, number);
  // snprintf honours the locale's decimal separator; JSON needs '.'.
  for (char* cursor = formatted; *cursor; ++cursor) {
    if (*cursor == ',') {
      *cursor = '.';
    }
  }
  m_output += formatted;

  return *this;
}

JsonWriter& JsonWriter::value(bool flag) {
  writeSeparator();
  m_output += jsonBooleanText(flag);

  return *this;
}

JsonWriter& JsonWriter::value(const JsonValue& parsedDocument) {
  switch (parsedDocument.type()) {
    case JsonValue::Type::Null: return null();
    case JsonValue::Type::Bool: return value(parsedDocument.asBool());
    case JsonValue::Type::String: return value(parsedDocument.asString());
    case JsonValue::Type::Number: {
      double number = parsedDocument.asNumber();
      bool isWholeNumber = std::trunc(number) == number && std::fabs(number) < kSafeInt64CastLimit;

      return isWholeNumber ? value(static_cast<int64_t>(number)) : value(number);
    }
    case JsonValue::Type::Array:
      beginArray();
      for (const JsonValue& item : parsedDocument.items()) {
        value(item);
      }
      return endArray();
    case JsonValue::Type::Object:
      beginObject();
      for (const auto& [name, member] : parsedDocument.members()) {
        key(name).value(member);
      }
      return endObject();
  }

  return null();
}

JsonWriter& JsonWriter::null() {
  writeSeparator();
  m_output += kJsonNull;

  return *this;
}

JsonWriter& JsonWriter::rawJson(std::string_view json) {
  writeSeparator();
  m_output += json;

  return *this;
}

}  // namespace tarkov_companion
