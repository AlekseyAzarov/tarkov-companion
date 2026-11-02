# libFuzzer targets for the code that reads untrusted input (docs/fuzzing.md).
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "The fuzzers need clang, which ships libFuzzer")
endif()

set(FUZZER_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
if(ANDROID OR IOS)
  # The core is built without RTTI there, so the vptr check misreads objects
  # it creates (shared_ptr control blocks) as having an invalid vptr.
  list(APPEND FUZZER_SANITIZER_FLAGS -fno-sanitize=vptr)
endif()
# The core gets coverage instrumentation; only the fuzzers link libFuzzer's main().
target_compile_options(tarkov_companion_core PRIVATE -fsanitize=fuzzer-no-link ${FUZZER_SANITIZER_FLAGS})

foreach(fuzzer json_fuzzer number_fuzzer bridge_message_fuzzer)
  add_executable(${fuzzer} ${fuzzer}.cpp)
  target_include_directories(${fuzzer} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../src ${TARKOV_COMPANION_GENERATED_DIR})
  target_link_libraries(${fuzzer} PRIVATE tarkov_companion_core)
  target_compile_options(${fuzzer} PRIVATE -fsanitize=fuzzer ${FUZZER_SANITIZER_FLAGS})
  target_link_options(${fuzzer} PRIVATE -fsanitize=fuzzer,address,undefined)
endforeach()
