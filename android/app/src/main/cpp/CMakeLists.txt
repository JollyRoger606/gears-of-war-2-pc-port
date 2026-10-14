# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Santiago Quiroz Upegui and the gow2-recomp contributors
#
# Placeholder native side of the Android shell: libSDL3.so plus libgow2shell.so (SDL_main), so the
# app can be built and run before the SDK and game libraries are packaged.

cmake_minimum_required(VERSION 3.22)
project(gow2shell LANGUAGES C CXX)

if(NOT GOW2_SDL3_SOURCE_DIR OR NOT EXISTS "${GOW2_SDL3_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "GOW2_SDL3_SOURCE_DIR must point to an SDL3 source tree "
                        "(gow2.sdl3.dir in android/local.properties); got '${GOW2_SDL3_SOURCE_DIR}'")
endif()

# SDL3 as libSDL3.so, the first library SDLActivity loads. The source tree is only read: its
# binary directory lives in this build tree.
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_ANDROID_JAR OFF CACHE BOOL "" FORCE)
add_subdirectory("${GOW2_SDL3_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/sdl3")

add_library(gow2shell SHARED gow2shell_main.cpp)
target_compile_features(gow2shell PRIVATE cxx_std_20)
target_compile_options(gow2shell PRIVATE -Wall -Wextra)
target_link_libraries(gow2shell PRIVATE SDL3::SDL3-shared vulkan log)
