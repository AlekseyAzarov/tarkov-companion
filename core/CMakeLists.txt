cmake_minimum_required(VERSION 3.22)

# Single source of truth for the release version, shared with Gradle.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../VERSION" TARKOV_COMPANION_VERSION)
string(STRIP "${TARKOV_COMPANION_VERSION}" TARKOV_COMPANION_VERSION)

project(tarkov_companion_core VERSION ${TARKOV_COMPANION_VERSION} LANGUAGES CXX)

option(TARKOV_COMPANION_BUILD_TESTS "Build GoogleTest suite" OFF)
option(TARKOV_COMPANION_SANITIZE "Build with ASan + UBSan (tests only)" OFF)

include(cmake/contract.cmake)
set(TARKOV_COMPANION_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
tarkov_companion_generate_contract(${TARKOV_COMPANION_GENERATED_DIR}/contract_generated.h)

add_library(tarkov_companion_core STATIC
  src/authentication.cpp
  src/bridge.cpp
  src/bridge_protocol.cpp
  src/c_api.cpp
  src/catalog.cpp
  src/engine.cpp
  src/favorites.cpp
  src/flea_fee.cpp
  src/json.cpp
  src/pkce.cpp
  src/recommender.cpp
  src/remote_flags.cpp
  src/session.cpp
  src/session_config.cpp
  src/tarkov_parser.cpp
  src/utf8.cpp
)
target_include_directories(tarkov_companion_core
  PUBLIC  ${CMAKE_CURRENT_LIST_DIR}/include
  PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src ${TARKOV_COMPANION_GENERATED_DIR}
)
target_compile_features(tarkov_companion_core PUBLIC cxx_std_17)
target_compile_definitions(tarkov_companion_core PRIVATE TARKOV_COMPANION_VERSION_STRING="${TARKOV_COMPANION_VERSION}")
set_target_properties(tarkov_companion_core PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)

if(MSVC)
  target_compile_options(tarkov_companion_core PRIVATE /W4)
else()
  target_compile_options(tarkov_companion_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

if(ANDROID OR IOS)
  # Size matters more than speed for code that runs once per lookup.
  target_compile_options(tarkov_companion_core PRIVATE -Os -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections)
endif()

if(TARKOV_COMPANION_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
