# Gears of War 2, Windows build.
#
# The recompiled game code in generated/ is produced from your own default.xex by
# `rexglue codegen` and is not part of this repository. scripts/build.ps1 does the whole sequence.

cmake_minimum_required(VERSION 3.25)
project(gow2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Written by `rexglue codegen`.
include(generated/rexglue.cmake)

set(GOW2_SOURCES
    src/main.cpp
    src/gow2_coalesced.cpp
    src/gow2_sha_table.cpp
    src/gow2_gpu_pick.cpp
    src/gow2_mouse_aim.cpp
    src/gow2_xam_input_guard.cpp
    src/gow2_utf8_guard.cpp
    src/gow2_crash_watch.cpp
    src/gow2_game_patches.cpp
    src/gow2_save_dir.cpp
    src/gow2_save_dir_setting.cpp
    src/gow2_script_patches.cpp
    src/gow2_script_patch_table.cpp
    src/gow2_script_patch_hook.cpp
    src/gow2_coop_census.cpp
    src/gow2_test_input.cpp
    src/gow2_test_input_hook.cpp
)

# The exe icon. CMake already turns on RC for clang on Windows; enable_language makes it explicit.
# The .rc names the .ico by path, so OBJECT_DEPENDS rebuilds the resource when the icon changes.
if(WIN32)
    enable_language(RC)
    list(APPEND GOW2_SOURCES src/gow2.rc)
    set_source_files_properties(src/gow2.rc PROPERTIES
        OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon/gow2.ico)
endif()

add_executable(gow2 WIN32 ${GOW2_SOURCES})

rexglue_setup_target(gow2 GPU_PLUGINS xenos)

# Host-side test of the script patcher on synthetic guest memory (no game data). Not part of the
# default build: cmake --build <dir> --target gow2_script_patch_test, then run the exe.
add_executable(gow2_script_patch_test EXCLUDE_FROM_ALL
    tools/tests/script_patch_core_test.cpp
    src/gow2_script_patches.cpp
    src/gow2_script_patch_table.cpp
)
target_include_directories(gow2_script_patch_test PRIVATE src)

# Host-side test of the focus-free test input parser and XINPUT_STATE merge.
add_executable(gow2_test_input_test EXCLUDE_FROM_ALL
    tools/tests/test_input_core_test.cpp
    src/gow2_test_input.cpp
)
target_include_directories(gow2_test_input_test PRIVATE src)

# SDL's gamepad mapping database; the input driver looks for it in the working directory.
add_custom_command(TARGET gow2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/config/gamecontrollerdb.txt
            $<TARGET_FILE_DIR:gow2>/gamecontrollerdb.txt
    COMMENT "Installing gamecontrollerdb.txt beside gow2.exe")

# Default settings, read by the runtime from gow2.toml beside the exe.
add_custom_command(TARGET gow2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/config/gow2.toml
            $<TARGET_FILE_DIR:gow2>/gow2.toml
    COMMENT "Installing gow2.toml beside gow2.exe")

# The SDK DLLs built from source (scripts/build-sdk.ps1 builds them into out/sdk). When they are
# there, they replace the stock rexruntime.dll / rexgpu-xenos.dll that the SDK stages next to the exe.
set(GOW2_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/out/sdk" CACHE PATH "Folder with the self-built rexruntime.dll and rexgpu-xenos.dll")
file(GLOB GOW2_FIDELITYFX_DLLS "${GOW2_SDK_DIR}/amd_fidelityfx_*.dll" "${GOW2_SDK_DIR}/nvngx_dlss.dll")
# A separate target that runs on every build: a POST_BUILD step only runs when gow2.exe relinks,
# so a rebuilt SDK would otherwise never reach the exe folder. It runs after gow2 so the stock
# DLLs that rexglue_setup_target stages there do not overwrite these.
if(EXISTS "${GOW2_SDK_DIR}/rexgpu-xenos.dll" AND EXISTS "${GOW2_SDK_DIR}/rexruntime.dll")
    add_custom_target(gow2_sdk_dlls ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GOW2_SDK_DIR}/rexgpu-xenos.dll" "${GOW2_SDK_DIR}/rexruntime.dll" ${GOW2_FIDELITYFX_DLLS}
                $<TARGET_FILE_DIR:gow2>
        COMMENT "Using the self-built SDK DLLs from ${GOW2_SDK_DIR}"
        VERBATIM)
    message(STATUS "Self-built SDK DLLs: ${GOW2_SDK_DIR}")
else()
    add_custom_target(gow2_sdk_dlls)
    message(STATUS "Self-built SDK DLLs: not built (run scripts/build-sdk.ps1); the stock SDK DLLs are used")
endif()
add_dependencies(gow2_sdk_dlls gow2)

# Post-codegen patchers. Each rewrites the generated sources before they are compiled, chained
# by stamps because they edit the same files.
#   switchtrap  jump tables codegen under-counted and unresolved branches: dispatch at runtime
#   gamepatch   xenia-canary community patches for this XEX, each behind a gow2_* cvar
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(GOW2_GEN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/generated/default)
set(_prev_stamp ${GOW2_GEN_DIR}/codegen.build.stamp)
macro(gow2_patch name script description)
    set(_stamp ${GOW2_GEN_DIR}/${name}.build.stamp)
    add_custom_command(OUTPUT ${_stamp}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/${script}
                ${GOW2_GEN_DIR}/*.cpp
        COMMAND ${CMAKE_COMMAND} -E touch ${_stamp}
        DEPENDS ${_prev_stamp}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "${description}"
        VERBATIM)
    add_custom_target(gow2_${name}_patch DEPENDS ${_stamp})
    add_dependencies(gow2_recomp gow2_${name}_patch)
    add_dependencies(gow2 gow2_${name}_patch)
    set(_prev_stamp ${_stamp})
endmacro()

gow2_patch(switchtrap patch_switch_traps.py "Patching switch traps in generated code")
gow2_patch(gamepatch patch_game_patches.py "Applying the game patches")

# A patcher that rewrites an already-compiled file after ninja planned its work would link a
# stale object and still report success. Fail the build in that case; build again.
add_custom_command(TARGET gow2 POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_objects_fresh.py
            ${GOW2_GEN_DIR}
            ${CMAKE_BINARY_DIR}/CMakeFiles/gow2_recomp.dir/generated/default
    COMMENT "Checking that no patched source is newer than its object"
    VERBATIM)

# Host-side test of the gow2_save_dir logic on temporary folders (no game data, no SDK). Not part
# of the default build: cmake --build <dir> --target gow2_save_dir_test, then run the exe.
add_executable(gow2_save_dir_test EXCLUDE_FROM_ALL
    tools/tests/save_dir_test.cpp
    src/gow2_save_dir.cpp
)
target_include_directories(gow2_save_dir_test PRIVATE src)
