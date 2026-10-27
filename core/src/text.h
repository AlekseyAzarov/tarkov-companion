// Small text checks shared by the session and the bridge protocol. Hand
// written rather than std::regex: that one is large, and it reports errors
// with exceptions, which the core is built without.
#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace tarkov_companion::text {

inline bool isAsciiSpace(char character) { return character == ' ' || (character >= '\t' && character <= '\r'); }
inline bool isLowercaseLetter(char character) { return character >= 'a' && character <= 'z'; }
inline bool isUppercaseLetter(char character) { return character >= 'A' && character <= 'Z'; }
inline bool isDigit(char character) { return character >= '0' && character <= '9'; }

inline bool isAlphanumeric(char character) {
  return isLowercaseLetter(character) || isUppercaseLetter(character) || isDigit(character);
}

inline std::string_view trimAsciiSpace(std::string_view text) {
  while (!text.empty() && isAsciiSpace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && isAsciiSpace(text.back())) {
    text.remove_suffix(1);
  }

  return text;
}

inline size_t codePointCount(std::string_view utf8Text) {
  constexpr unsigned char kContinuationByteMask = 0xC0;
  constexpr unsigned char kContinuationByteMarker = 0x80;

  return static_cast<size_t>(std::count_if(utf8Text.begin(), utf8Text.end(), [](char byte) {
    return (static_cast<unsigned char>(byte) & kContinuationByteMask) != kContinuationByteMarker;
  }));
}

inline bool startsWith(std::string_view text, std::string_view prefix) {
  return text.substr(0, prefix.size()) == prefix;
}

inline std::optional<int> hexDigitValue(char character) {
  constexpr int kValueOfLetterA = 10;

  if (isDigit(character)) {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f') {
    return character - 'a' + kValueOfLetterA;
  }
  if (character >= 'A' && character <= 'F') {
    return character - 'A' + kValueOfLetterA;
  }

  return std::nullopt;
}

inline constexpr int kHexBase = 16;

namespace detail {

inline void appendTwoHexDigitsPerByte(std::string& output, std::string_view bytes, const char* digitAlphabet) {
  for (char rawByte : bytes) {
    auto byte = static_cast<unsigned char>(rawByte);
    output.push_back(digitAlphabet[byte / kHexBase]);
    output.push_back(digitAlphabet[byte % kHexBase]);
  }
}

}  // namespace detail

inline void appendLowercaseHex(std::string& output, std::string_view bytes) {
  detail::appendTwoHexDigitsPerByte(output, bytes, "0123456789abcdef");
}

inline void appendUppercaseHex(std::string& output, std::string_view bytes) {
  detail::appendTwoHexDigitsPerByte(output, bytes, "0123456789ABCDEF");
}

// ASCII only: enough to make "Salewa" and "salewa" share a cache entry
// without pulling Unicode case tables into the binary.
inline std::string asciiLowercase(std::string_view text) {
  std::string lowercased(text);
  for (char& character : lowercased) {
    if (isUppercaseLetter(character)) {
      character = static_cast<char>(character - 'A' + 'a');
    }
  }

  return lowercased;
}

template <typename CharacterPredicate>
bool hasLengthAndOnlyAllowedCharacters(std::string_view text, size_t minimumLength, size_t maximumLength,
                                       CharacterPredicate isAllowedCharacter) {
  return text.size() >= minimumLength && text.size() <= maximumLength &&
         std::all_of(text.begin(), text.end(), isAllowedCharacter);
}

// Letters, digits, '_' and '-' only: safe inside cache keys, queries and protocol ids.
inline bool isSafeToken(std::string_view text, size_t maximumLength) {
  return hasLengthAndOnlyAllowedCharacters(text, 1, maximumLength, [](char character) {
    return isAlphanumeric(character) || character == '_' || character == '-';
  });
}

// tarkov.dev ids are 24 hex characters today; accept a little more, but never
// anything that could change the meaning of a cache key or a query.
inline constexpr size_t kItemIdMaximumLength = 64;

inline bool isItemId(std::string_view id) { return isSafeToken(id, kItemIdMaximumLength); }

}  // namespace tarkov_companion::text
