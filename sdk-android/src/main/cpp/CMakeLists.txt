cmake_minimum_required(VERSION 3.22)
project(tarkovcompanion LANGUAGES CXX)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../../core ${CMAKE_BINARY_DIR}/core)

option(TARKOV_COMPANION_STUB_DEMANGLER "Replace libc++abi's demangler with a stub (see demangle_stub.cpp)" ON)

add_library(tarkovcompanion SHARED jni_bridge.cpp)
if(TARKOV_COMPANION_STUB_DEMANGLER)
  target_sources(tarkovcompanion PRIVATE demangle_stub.cpp)
endif()
target_link_libraries(tarkovcompanion PRIVATE tarkov_companion_core log)
target_compile_options(tarkovcompanion PRIVATE
  -Os -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -fvisibility=hidden)
target_link_options(tarkovcompanion PRIVATE
  -Wl,--gc-sections
  -Wl,--exclude-libs,ALL
  # Export JNI_OnLoad only. Natives are bound with RegisterNatives, so no
  # Java_* symbols are needed and our static libc++ never leaks into the
  # host's symbol namespace.
  -Wl,--version-script=${CMAKE_CURRENT_LIST_DIR}/exports.map
  # Android 15+ devices may use 16 KB pages.
  -Wl,-z,max-page-size=16384)
