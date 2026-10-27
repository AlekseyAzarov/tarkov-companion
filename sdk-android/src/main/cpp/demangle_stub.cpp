// Keeps libc++abi's demangler (~118 KB on arm64) out of the library: defining
// the symbol here stops the linker pulling the real one. It is reachable only
// from the terminate handler's message for an uncaught exception, which the
// SDK, built with -fno-exceptions, can't produce (docs/footprint.md).
#include <cstddef>

namespace {
// Itanium C++ ABI status code: "mangled_name is not a valid name".
constexpr int kInvalidMangledName = -2;
}  // namespace

extern "C" char* __cxa_demangle(const char*, char*, size_t*, int* status) {
  if (status) *status = kInvalidMangledName;
  return nullptr;
}
