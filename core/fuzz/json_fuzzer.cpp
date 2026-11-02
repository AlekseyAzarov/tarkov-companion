// Any bytes: parsing must not crash, a parsed document written back must parse
// again, and from then on writing it back must not change the text. The first
// write can: "%.6g" turns 919976123456.5 into 9.19976e+11, which reads back as
// the whole number 919976000000.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "json.h"

namespace tarkov_companion {
namespace {

std::string writeBack(const JsonValue& document) {
  JsonWriter writer;
  writer.value(document);

  return writer.takeText();
}

// A trap rather than abort(): libFuzzer on Android doesn't catch SIGABRT and
// would not save the input.
[[noreturn]] void fail(const char* what, const std::string& written) {
  std::fprintf(stderr, "%s\nwritten: %s\n", what, written.c_str());
  __builtin_trap();
}

std::string writeBackParsed(const std::string& written) {
  JsonParseResult reparsed = parseJson(written);
  if (!reparsed.ok) {
    fail(reparsed.error, written);
  }

  return writeBack(reparsed.value);
}

}  // namespace
}  // namespace tarkov_companion

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  using namespace tarkov_companion;

  std::string_view input(reinterpret_cast<const char*>(data), size);
  JsonParseResult parsed = parseJson(input);
  if (!parsed.ok) {
    return 0;
  }

  std::string normalized = writeBackParsed(writeBack(parsed.value));
  if (writeBackParsed(normalized) != normalized) {
    fail("writing the reparsed document gave different text", normalized);
  }

  return 0;
}
