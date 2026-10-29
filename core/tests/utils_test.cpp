// The helpers shared across the core: text.h, utf8.h, clock.h.
#include <gtest/gtest.h>

#include <string>

#include "clock.h"
#include "text.h"
#include "utf8.h"

namespace tarkov_companion {
namespace {

TEST(TextTest, StartsWith) {
  EXPECT_TRUE(text::startsWith("favorites|account|42", "favorites|account|"));
  EXPECT_TRUE(text::startsWith("abc", ""));
  EXPECT_FALSE(text::startsWith("ab", "abc"));
  EXPECT_FALSE(text::startsWith("https://x", "http://"));
}

TEST(TextTest, HexDigitValue) {
  EXPECT_EQ(text::hexDigitValue('0'), 0);
  EXPECT_EQ(text::hexDigitValue('9'), 9);
  EXPECT_EQ(text::hexDigitValue('a'), 10);
  EXPECT_EQ(text::hexDigitValue('F'), 15);
  EXPECT_EQ(text::hexDigitValue('g'), std::nullopt);
  EXPECT_EQ(text::hexDigitValue('%'), std::nullopt);
}

TEST(TextTest, HexInEitherCase) {
  std::string lower = "file-";
  text::appendLowercaseHex(lower, "gra\xFF");
  EXPECT_EQ(lower, "file-677261ff");

  std::string upper;
  text::appendUppercaseHex(upper, "/\xFF");
  EXPECT_EQ(upper, "2FFF");

  std::string empty;
  text::appendLowercaseHex(empty, "");
  EXPECT_EQ(empty, "");
}

TEST(Utf8Test, EveryLengthRoundTrips) {
  // 1, 2, 3 and 4 bytes: "a", "é", "€", "😀".
  for (char32_t code : {U'a', U'é', U'€', U'\U0001F600'}) {
    std::string encoded;
    utf8::append(encoded, code);
    size_t pos = 0;
    EXPECT_EQ(utf8::decodeAndAdvance(encoded, &pos), code);
    EXPECT_EQ(pos, encoded.size());
  }

  std::string emoji;
  utf8::append(emoji, U'\U0001F600');
  EXPECT_EQ(emoji, "\xF0\x9F\x98\x80");
}

TEST(Utf8Test, MalformedInputIsReplacedOneByteAtATime) {
  // A stray continuation byte, a lead byte cut short by the end, and a lead
  // byte followed by ASCII: each costs one byte, and decoding resumes after it.
  std::string text = "\x80" "a\xC3" "b\xE2";
  size_t pos = 0;
  EXPECT_EQ(utf8::decodeAndAdvance(text, &pos), utf8::kReplacementCharacter);
  EXPECT_EQ(utf8::decodeAndAdvance(text, &pos), U'a');
  EXPECT_EQ(utf8::decodeAndAdvance(text, &pos), utf8::kReplacementCharacter);
  EXPECT_EQ(utf8::decodeAndAdvance(text, &pos), U'b');
  EXPECT_EQ(utf8::decodeAndAdvance(text, &pos), utf8::kReplacementCharacter);
  EXPECT_EQ(pos, text.size());
}

TEST(Utf8Test, OverlongSurrogateAndOutOfRangeSequencesAreMalformed) {
  // Overlong "A" (2 bytes), overlong "/" (3 bytes), overlong U+FFFF (4 bytes),
  // the surrogate U+D800, U+110000, and a lead byte that could only start it.
  for (std::string sequence : {"\xC1\x81", "\xE0\x80\xAF", "\xF0\x8F\xBF\xBF", "\xED\xA0\x80",
                               "\xF4\x90\x80\x80", "\xF5\x80\x80\x80"}) {
    size_t position = 0;
    EXPECT_EQ(utf8::decodeAndAdvance(sequence, &position), utf8::kReplacementCharacter);
    EXPECT_EQ(position, 1u);
  }
}

TEST(Utf8Test, SmallestAndLargestCodePointOfEachLengthDecode) {
  struct Case {
    std::string encoded;
    char32_t codePoint;
  };
  for (const Case& testCase : {Case{"\xC2\x80", 0x80}, Case{"\xDF\xBF", 0x7FF}, Case{"\xE0\xA0\x80", 0x800},
                               Case{"\xEF\xBF\xBF", 0xFFFF}, Case{"\xF0\x90\x80\x80", 0x10000},
                               Case{"\xF4\x8F\xBF\xBF", 0x10FFFF}}) {
    size_t position = 0;
    EXPECT_EQ(utf8::decodeAndAdvance(testCase.encoded, &position), testCase.codePoint);
    EXPECT_EQ(position, testCase.encoded.size());
  }
}

TEST(ClockTest, EpochMillis) {
  EXPECT_EQ(millisecondsSinceEpoch(SystemTime(std::chrono::milliseconds(1793108040123))), 1793108040123);
  EXPECT_EQ(millisecondsSinceEpoch(SystemTime()), 0);
}

}  // namespace
}  // namespace tarkov_companion
