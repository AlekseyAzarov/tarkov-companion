cmake_minimum_required(VERSION 3.22)
project(hostgame LANGUAGES C CXX)

add_library(native_app_glue STATIC
  ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC
  ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(hostgame SHARED game_main.cpp renderer.cpp)
target_compile_features(hostgame PRIVATE cxx_std_17)
target_compile_options(hostgame PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
target_link_libraries(hostgame PRIVATE native_app_glue android EGL GLESv2 log)
target_link_options(hostgame PRIVATE
  # native_app_glue's entry point would otherwise be stripped as unused.
  -u ANativeActivity_onCreate
  -Wl,-z,max-page-size=16384)
