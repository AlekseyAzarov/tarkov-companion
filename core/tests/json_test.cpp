#include "json.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <random>
#include <string>

namespace tarkov_companion {
namespace {

TEST(JsonParse, ScalarsAndNesting) {
  auto parsed = parseJson(R"({"a":1,"b":[true,false,null],"c":{"d":"x"},"e":-2.5e2})");
  ASSERT_TRUE(parsed.ok) << parsed.error;
  const JsonValue& root = parsed.value;
  EXPECT_EQ(root["a"].asInteger(), 1);
  ASSERT_EQ(root["b"].items().size(), 3u);
  EXPECT_TRUE(root["b"].items()[0].asBool());
  EXPECT_TRUE(root["b"].items()[2].isNull());
  EXPECT_EQ(root["c"]["d"].asString(), "x");
  EXPECT_DOUBLE_EQ(root["e"].asNumber(), -250.0);
}

TEST(JsonParse, MissingKeysChainToNull) {
  auto parsed = parseJson(R"({"data":null})");
  ASSERT_TRUE(parsed.ok);
  EXPECT_TRUE(parsed.value["data"]["items"].isNull());
  EXPECT_TRUE(parsed.value["data"]["items"].items().empty());
  EXPECT_EQ(parsed.value["nope"].asString("fallback"), "fallback");
}

TEST(JsonParse, DecodesEscapesAndSurrogatePairs) {
  auto parsed = parseJson(R"(["a\"b\\c\n", "\u041f\u0440\u0438", "\ud83d\ude00"])");
  ASSERT_TRUE(parsed.ok) << parsed.error;
  EXPECT_EQ(parsed.value.items()[0].asString(), "a\"b\\c\n");
  EXPECT_EQ(parsed.value.items()[1].asString(), "\xD0\x9F\xD1\x80\xD0\xB8");  // "При"
  EXPECT_EQ(parsed.value.items()[2].asString(), "\xF0\x9F\x98\x80");          // U+1F600
}

TEST(JsonParse, PassesThroughRawUtf8) {
  auto parsed = parseJson("\"Аптечка\"");
  ASSERT_TRUE(parsed.ok);
  EXPECT_EQ(parsed.value.asString(), "Аптечка");
}

class JsonRejects : public ::testing::TestWithParam<const char*> {};

TEST_P(JsonRejects, Invalid) {
  EXPECT_FALSE(parseJson(GetParam()).ok) << GetParam();
}

INSTANTIATE_TEST_SUITE_P(Malformed, JsonRejects,
                         ::testing::Values("", "{", "[1,]", "{\"a\":1,}", "{'a':1}", "01", "1.",
                                           "-", "tru", "\"unterminated", "\"\\x\"",
                                           "\"\\ud83d\"", "\"\\ude00\"", "[1] 2", "nan",
                                           "\"tab\there\""));

TEST(JsonParse, LongNumbersStayFinite) {
  // 400 significant digits after the point once overflowed the mantissa to
  // inf, and inf * 10^-400 is NaN.
  auto longFraction = parseJson("0." + std::string(400, '1'));
  ASSERT_TRUE(longFraction.ok) << longFraction.error;
  EXPECT_DOUBLE_EQ(longFraction.value.asNumber(), 0.11111111111111111);

  auto longInteger = parseJson("12345678901234567890123");
  ASSERT_TRUE(longInteger.ok) << longInteger.error;
  EXPECT_DOUBLE_EQ(longInteger.value.asNumber(), 1.2345678901234567e22);

  auto tinyFraction = parseJson("0." + std::string(400, '0') + "1");
  ASSERT_TRUE(tinyFraction.ok) << tinyFraction.error;
  EXPECT_EQ(tinyFraction.value.asNumber(), 0.0);

  auto zeroWithHugeExponent = parseJson("0e400");
  ASSERT_TRUE(zeroWithHugeExponent.ok) << zeroWithHugeExponent.error;
  EXPECT_EQ(zeroWithHugeExponent.value.asNumber(), 0.0);
}

TEST(JsonParse, ShortDecimalsAreCorrectlyRounded) {
  EXPECT_EQ(parseJson("0.3").value.asNumber(), 0.3);
  EXPECT_EQ(parseJson("1.1").value.asNumber(), 1.1);
  EXPECT_EQ(parseJson("-2.675e-3").value.asNumber(), -2.675e-3);

  // Up to 15 significant digits and a power of ten within ±22, against the C
  // library's correctly rounded strtod.
  std::mt19937_64 random(20261102);
  std::uniform_int_distribution<uint64_t> mantissas(0, 999'999'999'999'999);
  for (int attempt = 0; attempt < 100'000; ++attempt) {
    std::string digits = std::to_string(mantissas(random));
    int digitCount = static_cast<int>(digits.size());
    int fractionDigits = std::uniform_int_distribution<int>(0, digitCount)(random);
    int exponent = std::uniform_int_distribution<int>(fractionDigits - 22, fractionDigits + 22)(random);

    std::string integerPart = digits.substr(0, digits.size() - static_cast<size_t>(fractionDigits));
    std::string text = integerPart.empty() ? "0" : integerPart;
    if (fractionDigits > 0) {
      text += "." + digits.substr(integerPart.size());
    }
    if (exponent != 0) {
      text += "e" + std::to_string(exponent);
    }

    auto parsed = parseJson(text);
    ASSERT_TRUE(parsed.ok) << text;
    ASSERT_EQ(parsed.value.asNumber(), std::strtod(text.c_str(), nullptr)) << text;
  }
}

TEST(JsonParse, KeepsTheDigitsOfSubnormals) {
  // 10^-326 alone underflows to zero, though the value itself is representable.
  EXPECT_EQ(parseJson("9.99989e-321").value.asNumber(), std::strtod("9.99989e-321", nullptr));
}

TEST(JsonParse, DepthLimitStopsStackExhaustion) {
  const std::string deeplyNested(10000, '[');
  auto parsed = parseJson(deeplyNested);
  EXPECT_FALSE(parsed.ok);
  EXPECT_STREQ(parsed.error, "nesting too deep");
}

TEST(JsonParse, ReportsErrorOffset) {
  const std::string input = R"({"a": tru})";
  auto parsed = parseJson(input);
  EXPECT_FALSE(parsed.ok);
  EXPECT_EQ(parsed.errorOffset, input.find("tru"));
}

TEST(JsonWriter, ProducesParseableOutput) {
  JsonWriter writer;
  writer.beginObject();
  writer.key("s").value("quote\" and \\ and \x01");
  writer.key("n").value(int64_t{42});
  writer.key("d").value(0.5);
  writer.key("arr").beginArray().value(true).null().beginObject().endObject().endArray();
  writer.endObject();

  auto parsed = parseJson(writer.text());
  ASSERT_TRUE(parsed.ok) << writer.text();
  EXPECT_EQ(parsed.value["s"].asString(), "quote\" and \\ and \x01");
  EXPECT_EQ(parsed.value["n"].asInteger(), 42);
  EXPECT_DOUBLE_EQ(parsed.value["d"].asNumber(), 0.5);
  EXPECT_EQ(parsed.value["arr"].items().size(), 3u);
}

TEST(JsonWriter, WritesAParsedDocumentBack) {
  std::string text = R"({"a":[1,-2.5,true,null,"x\"y"],"b":{"price":498000,"big":1e20},"c":[]})";
  JsonWriter writer;
  writer.value(parseJson(text).value);
  EXPECT_EQ(writer.text(), R"({"a":[1,-2.5,true,null,"x\"y"],"b":{"price":498000,"big":1e+20},"c":[]})");
}

}  // namespace
}  // namespace tarkov_companion
