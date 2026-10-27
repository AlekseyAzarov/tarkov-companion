include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(tarkov_companion_core_tests
  authentication_test.cpp
  bridge_protocol_test.cpp
  bridge_test.cpp
  bridge_transcript_test.cpp
  c_api_test.cpp
  catalog_test.cpp
  engine_test.cpp
  favorites_test.cpp
  flea_fee_test.cpp
  json_test.cpp
  lru_cache_test.cpp
  parser_test.cpp
  pkce_test.cpp
  recommender_test.cpp
  remote_flags_test.cpp
  session_config_test.cpp
  session_test.cpp
  utils_test.cpp
)
target_include_directories(tarkov_companion_core_tests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../src ${TARKOV_COMPANION_GENERATED_DIR})
target_link_libraries(tarkov_companion_core_tests PRIVATE tarkov_companion_core GTest::gtest_main)
target_compile_definitions(tarkov_companion_core_tests PRIVATE
  TARKOV_COMPANION_FIXTURES_DIR="${CMAKE_CURRENT_LIST_DIR}/../../contracts/fixtures")

if(TARKOV_COMPANION_SANITIZE AND NOT MSVC)
  foreach(t tarkov_companion_core tarkov_companion_core_tests)
    target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(${t} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()

if(CMAKE_CROSSCOMPILING)
  # Android builds are pushed to a device and run there (scripts/run-core-tests-on-device.sh).
  add_test(NAME tarkov_companion_core_tests COMMAND tarkov_companion_core_tests)
else()
  include(GoogleTest)
  gtest_discover_tests(tarkov_companion_core_tests)
endif()
