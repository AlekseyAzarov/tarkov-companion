#include "catalog.h"

#include <algorithm>

#include "contract_generated.h"
#include "json.h"
#include "tarkov_api.h"
#include "text.h"
#include "utf8.h"

namespace tarkov_companion::catalog {
namespace {

// The languages the catalog is built in (scripts/catalog/catalog.mjs); others read English.
constexpr const char* kCatalogLanguageEnglish = "en";
constexpr const char* kCatalogLanguageRussian = "ru";

constexpr char32_t kLatin1UppercaseFirst = 0xC0;
constexpr char32_t kLatin1UppercaseLast = 0xDE;
constexpr char32_t kLatinLetterLast = 0x24F;
constexpr char32_t kMultiplicationSign = 0xD7;
constexpr char32_t kDivisionSign = 0xF7;
constexpr char32_t kCyrillicFirst = 0x400;
constexpr char32_t kCyrillicLast = 0x4FF;
constexpr char32_t kCyrillicUppercaseFirst = 0x410;
constexpr char32_t kCyrillicUppercaseLast = 0x42F;
constexpr char32_t kCyrillicExtendedUppercaseLast = 0x40F;
constexpr char32_t kCyrillicLowercaseYo = 0x451;
constexpr char32_t kCyrillicLowercaseYe = 0x435;
constexpr char32_t kCaseOffset = 0x20;
constexpr char32_t kCyrillicExtendedCaseOffset = 0x50;

char32_t lowercaseWithYoFoldedIntoYe(char32_t codePoint) {
  if (codePoint >= 'A' && codePoint <= 'Z') {
    return codePoint + kCaseOffset;
  }
  bool isLatin1Uppercase = codePoint >= kLatin1UppercaseFirst && codePoint <= kLatin1UppercaseLast;
  if (isLatin1Uppercase && codePoint != kMultiplicationSign) {
    return codePoint + kCaseOffset;
  }
  if (codePoint >= kCyrillicUppercaseFirst && codePoint <= kCyrillicUppercaseLast) {
    return codePoint + kCaseOffset;
  }
  if (codePoint >= kCyrillicFirst && codePoint <= kCyrillicExtendedUppercaseLast) {
    codePoint += kCyrillicExtendedCaseOffset;
  }

  return codePoint == kCyrillicLowercaseYo ? kCyrillicLowercaseYe : codePoint;
}

bool isWordCharacter(char32_t codePoint) {
  if (codePoint < 0x80) {
    return text::isAlphanumeric(static_cast<char>(codePoint));
  }
  if (codePoint >= kLatin1UppercaseFirst && codePoint <= kLatinLetterLast) {
    return codePoint != kMultiplicationSign && codePoint != kDivisionSign;
  }

  return codePoint >= kCyrillicFirst && codePoint <= kCyrillicLast;
}

std::string_view firstCodePointsOfValidUtf8(std::string_view word, size_t count) {
  size_t position = 0;
  for (size_t taken = 0; taken < count && position < word.size(); ++taken) {
    utf8::decodeAndAdvance(word, &position);
  }

  return word.substr(0, position);
}

}  // namespace

std::vector<std::string> lowercaseSearchWords(std::string_view text) {
  std::vector<std::string> words;
  std::string word;
  size_t position = 0;
  while (position < text.size()) {
    char32_t codePoint = lowercaseWithYoFoldedIntoYe(utf8::decodeAndAdvance(text, &position));
    if (isWordCharacter(codePoint)) {
      utf8::append(word, codePoint);
    } else if (!word.empty()) {
      words.push_back(std::move(word));
      word.clear();
    }
  }
  if (!word.empty()) {
    words.push_back(std::move(word));
  }

  return words;
}

std::optional<std::string> searchShardFileName(std::string_view query) {
  std::string longestWord;
  size_t longestLength = 0;
  for (std::string& word : lowercaseSearchWords(query)) {
    size_t length = text::codePointCount(word);
    if (length > longestLength) {
      longestWord = std::move(word);
      longestLength = length;
    }
  }
  if (longestLength < static_cast<size_t>(contract::kCatalogShardMinimumLetters)) {
    return std::nullopt;
  }

  size_t letters = std::min(longestLength, static_cast<size_t>(contract::kCatalogShardLetters));
  // Lowercase, as the generator's Buffer.toString("hex") writes the file names.
  std::string fileName;
  text::appendLowercaseHex(fileName, firstCodePointsOfValidUtf8(longestWord, letters));

  return fileName + tarkov_api::kCatalogFileExtension;
}

bool everyQueryWordStartsAWordOf(const std::vector<std::string>& queryWords, std::string_view name,
                                 std::string_view shortName) {
  std::vector<std::string> words = lowercaseSearchWords(name);
  std::vector<std::string> shortNameWords = lowercaseSearchWords(shortName);
  words.insert(words.end(), shortNameWords.begin(), shortNameWords.end());

  return std::all_of(queryWords.begin(), queryWords.end(), [&](const std::string& queryWord) {
    return std::any_of(words.begin(), words.end(),
                       [&](const std::string& word) { return text::startsWith(word, queryWord); });
  });
}

std::optional<std::string> searchResponseFromShard(std::string_view shardJson, std::string_view query,
                                                   size_t maximumItems) {
  JsonParseResult parsed = parseJson(shardJson);
  const JsonValue& items = parsed.value[tarkov_api::kFieldData][tarkov_api::kFieldItems];
  if (!parsed.ok || !items.isArray()) {
    return std::nullopt;
  }

  std::vector<std::string> queryWords = lowercaseSearchWords(query);
  JsonWriter response;
  response.beginObject().key(tarkov_api::kFieldData).beginObject().key(tarkov_api::kFieldItems).beginArray();
  size_t keptItems = 0;
  for (const JsonValue& item : items.items()) {
    if (keptItems == maximumItems) {
      break;
    }
    std::string_view name = item[tarkov_api::kFieldName].asString();
    std::string_view shortName = item[tarkov_api::kFieldShortName].asString();
    if (!everyQueryWordStartsAWordOf(queryWords, name, shortName)) {
      continue;
    }
    response.value(item);
    ++keptItems;
  }
  response.endArray().endObject().endObject();

  return response.takeText();
}

const char* catalogLanguageFor(std::string_view configuredLanguage) {
  return configuredLanguage == kCatalogLanguageRussian ? kCatalogLanguageRussian : kCatalogLanguageEnglish;
}

}  // namespace tarkov_companion::catalog
