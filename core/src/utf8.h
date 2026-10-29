#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace tarkov_companion::utf8 {

inline constexpr char32_t kReplacementCharacter = 0xFFFD;

// Malformed input (including overlong forms, surrogates and code points past
// U+10FFFF) comes back as kReplacementCharacter, one byte at a time, so a
// caller can treat it like any other symbol.
char32_t decodeAndAdvance(std::string_view text, size_t* position);

void append(std::string& output, char32_t codePoint);

}  // namespace tarkov_companion::utf8
