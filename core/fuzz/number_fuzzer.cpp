// Differential check of JSON numbers against strtod, the C library's
// correctly rounded conversion. Short decimals (at most 15 significant digits,
// power of ten within ±22) must match it bit for bit; other normal doubles
// must stay within a few ulps.
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "json.h"

namespace tarkov_companion {
namespace {

constexpr int kShortDecimalSignificantDigits = 15;
constexpr int kShortDecimalPowerOfTen = 22;
// A longer exponent is far outside the short decimal range anyway.
constexpr size_t kExponentDigitsToRead = 4;

constexpr double kRelativeToleranceElsewhere = 4 * DBL_EPSILON;
// Beyond these the parser may round to zero or infinity early; no price or
// ratio gets near them.
constexpr double kSmallestComparedMagnitude = 1e-280;
constexpr double kLargestComparedMagnitude = 1e280;

constexpr std::string_view kJsonWhitespace = " \t\n\r";

struct DecimalShape {
  int significantDigits = 0;
  // Of the last digit written: "1.25e3" is 125 * 10^1.
  int powerOfTen = 0;
  bool exponentTooLong = false;
};

// The text has already parsed as one JSON number, so its grammar holds.
DecimalShape shapeOf(std::string_view number) {
  DecimalShape shape;
  bool seenNonZeroDigit = false;
  bool inFraction = false;

  size_t position = 0;
  for (; position < number.size(); ++position) {
    char character = number[position];
    if (character == 'e' || character == 'E') {
      break;
    }
    if (character == '-') {
      continue;
    }
    if (character == '.') {
      inFraction = true;
      continue;
    }

    if (character != '0') {
      seenNonZeroDigit = true;
    }
    if (seenNonZeroDigit) {
      ++shape.significantDigits;
    }
    if (inFraction) {
      --shape.powerOfTen;
    }
  }

  if (position == number.size()) {
    return shape;
  }

  ++position;
  bool negativeExponent = number[position] == '-';
  if (number[position] == '-' || number[position] == '+') {
    ++position;
  }
  std::string_view exponentDigits = number.substr(position);
  exponentDigits.remove_prefix(std::min(exponentDigits.find_first_not_of('0'), exponentDigits.size()));
  if (exponentDigits.size() > kExponentDigitsToRead) {
    shape.exponentTooLong = true;
    return shape;
  }

  int exponent = exponentDigits.empty() ? 0 : std::atoi(std::string(exponentDigits).c_str());
  shape.powerOfTen += negativeExponent ? -exponent : exponent;

  return shape;
}

bool isShortDecimal(const DecimalShape& shape) {
  return !shape.exponentTooLong && shape.significantDigits <= kShortDecimalSignificantDigits &&
         std::abs(shape.powerOfTen) <= kShortDecimalPowerOfTen;
}

// A trap rather than abort(): libFuzzer on Android doesn't catch SIGABRT and
// would not save the input.
[[noreturn]] void fail(const char* what, const std::string& number, double parsed, double expected) {
  std::fprintf(stderr, "%s: \"%s\" parsed as %.17g, strtod gives %.17g\n", what, number.c_str(), parsed, expected);
  __builtin_trap();
}

}  // namespace
}  // namespace tarkov_companion

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  using namespace tarkov_companion;

  std::string_view input(reinterpret_cast<const char*>(data), size);
  JsonParseResult parsed = parseJson(input);
  if (!parsed.ok || !parsed.value.isNumber()) {
    return 0;
  }

  size_t numberStart = input.find_first_not_of(kJsonWhitespace);
  size_t numberEnd = input.find_last_not_of(kJsonWhitespace) + 1;
  std::string number(input.substr(numberStart, numberEnd - numberStart));
  double parsedNumber = parsed.value.asNumber();
  double expected = std::strtod(number.c_str(), nullptr);

  if (isShortDecimal(shapeOf(number))) {
    bool sameBits = parsedNumber == expected && std::signbit(parsedNumber) == std::signbit(expected);
    if (!sameBits) {
      fail("short decimal not correctly rounded", number, parsedNumber, expected);
    }
    return 0;
  }

  double magnitude = std::fabs(expected);
  if (magnitude < kSmallestComparedMagnitude || magnitude > kLargestComparedMagnitude) {
    return 0;
  }
  if (std::fabs(parsedNumber - expected) > kRelativeToleranceElsewhere * magnitude) {
    fail("more than a few ulps off", number, parsedNumber, expected);
  }

  return 0;
}
