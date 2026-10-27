#pragma once

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace tarkov_companion::test {

// Fixtures are shared with the Kotlin tests and the live contract check.
// TARKOV_COMPANION_FIXTURES_DIR in the environment overrides the compiled-in path, which is
// how the suite finds them when it runs on a device.
inline std::string readFixture(const std::string& name) {
  const char* overrideDir = std::getenv("TARKOV_COMPANION_FIXTURES_DIR");
  std::ifstream file(std::string(overrideDir ? overrideDir : TARKOV_COMPANION_FIXTURES_DIR) + "/" + name,
                     std::ios::binary);
  std::stringstream contents;
  contents << file.rdbuf();

  return contents.str();
}

}  // namespace tarkov_companion::test
