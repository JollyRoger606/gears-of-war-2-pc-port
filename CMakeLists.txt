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
    src/gow2_xam_input_guard.cpp
    src/gow2_utf8_guard.cpp
    src/gow2_crash_watch.cpp
)

add_executable(gow2 WIN32 ${GOW2_SOURCES})

rexglue_setup_target(gow2 GPU_PLUGINS xenos)

# SDL's gamepad mapping database; the input driver looks for it in the working directory.
add_custom_command(TARGET gow2 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/config/gamecontrollerdb.txt
            $<TARGET_FILE_DIR:gow2>/gamecontrollerdb.txt
    COMMENT "Installing gamecontrollerdb.txt beside gow2.exe")

# Post-codegen patchers. Each rewrites the generated sources before they are compiled, chained
# by stamps because they edit the same files.
#   switchtrap  jump tables codegen under-counted and unresolved branches: dispatch at runtime
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

# A patcher that rewrites an already-compiled file after ninja planned its work would link a
# stale object and still report success. Fail the build in that case; build again.
add_custom_command(TARGET gow2 POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_objects_fresh.py
            ${GOW2_GEN_DIR}
            ${CMAKE_BINARY_DIR}/CMakeFiles/gow2_recomp.dir/generated/default
    COMMENT "Checking that no patched source is newer than its object"
    VERBATIM)
