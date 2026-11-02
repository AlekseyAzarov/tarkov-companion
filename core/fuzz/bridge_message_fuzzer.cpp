// A hostile page's message: validation must not crash, and the reply the
// native side sends back, carrying the page's id, must be valid JSON.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "bridge_protocol.h"
#include "json.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  using namespace tarkov_companion;

  std::string_view message(reinterpret_cast<const char*>(data), size);
  bridge::ParsedMessage parsed = bridge::parseMessage(message);

  std::string reply = parsed.validRequest
                          ? bridge::okMessage(parsed.validRequest->id, kEmptyJsonObject)
                          : bridge::errorMessage(parsed.idToAnswerWithError, parsed.errorCode, parsed.errorMessage);
  if (!parseJson(reply).ok) {
    std::fprintf(stderr, "reply is not valid JSON: %s\n", reply.c_str());
    // A trap rather than abort(): libFuzzer on Android doesn't catch SIGABRT.
    __builtin_trap();
  }

  return 0;
}
