#include "utf8.h"

namespace tarkov_companion::utf8 {
namespace {

constexpr char32_t kLargestOneByteCodePoint = 0x7F;
constexpr char32_t kLargestTwoByteCodePoint = 0x7FF;
constexpr char32_t kLargestThreeByteCodePoint = 0xFFFF;
constexpr char32_t kTwoByteLead = 0xC0;
constexpr char32_t kThreeByteLead = 0xE0;
constexpr char32_t kFourByteLead = 0xF0;
constexpr char32_t kContinuationMarker = 0x80;
constexpr char32_t kContinuationPayloadMask = 0x3F;
constexpr int kContinuationPayloadBits = 6;

size_t sequenceLengthFromLeadByte(char32_t lead) {
  if (lead <= kLargestOneByteCodePoint) {
    return 1;
  }
  if ((lead & 0xE0) == kTwoByteLead) {
    return 2;
  }
  if ((lead & 0xF0) == kThreeByteLead) {
    return 3;
  }
  if ((lead & 0xF8) == kFourByteLead) {
    return 4;
  }

  return 0;
}

}  // namespace

char32_t decodeAndAdvance(std::string_view text, size_t* position) {
  auto byteAt = [&](size_t index) { return static_cast<char32_t>(static_cast<unsigned char>(text[index])); };

  char32_t lead = byteAt(*position);
  size_t length = sequenceLengthFromLeadByte(lead);
  if (length == 0 || *position + length > text.size()) {
    ++*position;
    return kReplacementCharacter;
  }

  char32_t codePoint = length == 1 ? lead : lead & (0x7F >> length);
  for (size_t index = 1; index < length; ++index) {
    char32_t next = byteAt(*position + index);
    if ((next & ~kContinuationPayloadMask) != kContinuationMarker) {
      ++*position;
      return kReplacementCharacter;
    }
    codePoint = (codePoint << kContinuationPayloadBits) | (next & kContinuationPayloadMask);
  }
  *position += length;

  return codePoint;
}

void append(std::string& output, char32_t codePoint) {
  auto appendByte = [&output](char32_t value) { output.push_back(static_cast<char>(value)); };
  auto appendContinuation = [&](int shift) {
    appendByte(kContinuationMarker | ((codePoint >> shift) & kContinuationPayloadMask));
  };

  if (codePoint <= kLargestOneByteCodePoint) {
    appendByte(codePoint);
  } else if (codePoint <= kLargestTwoByteCodePoint) {
    appendByte(kTwoByteLead | (codePoint >> kContinuationPayloadBits));
    appendContinuation(0);
  } else if (codePoint <= kLargestThreeByteCodePoint) {
    appendByte(kThreeByteLead | (codePoint >> (2 * kContinuationPayloadBits)));
    appendContinuation(kContinuationPayloadBits);
    appendContinuation(0);
  } else {
    appendByte(kFourByteLead | (codePoint >> (3 * kContinuationPayloadBits)));
    appendContinuation(2 * kContinuationPayloadBits);
    appendContinuation(kContinuationPayloadBits);
    appendContinuation(0);
  }
}

}  // namespace tarkov_companion::utf8
