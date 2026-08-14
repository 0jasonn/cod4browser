cmake_minimum_required(VERSION 3.16)

project(KisakCOD)

option(KISAK_PORTABLE_TESTS_ONLY "Build only platform-neutral unit tests" OFF)
if (KISAK_PORTABLE_TESTS_ONLY)
    include(CTest)
    add_subdirectory(tests/native)
    return()
endif()

# Set the default C++ standard
set(CMAKE_CXX_STANDARD 20)

add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)

# Select a platform explicitly. Native builds keep the historical win32
# default, while an Emscripten toolchain defaults to the isolated web target.
set(_KISAK_PLATFORM_DEFAULT win32)
if (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(_KISAK_PLATFORM_DEFAULT web)
endif()

set(KISAK_PLATFORM "${_KISAK_PLATFORM_DEFAULT}" CACHE STRING "KisakCOD target platform")
set_property(CACHE KISAK_PLATFORM PROPERTY STRINGS win32 web)
unset(_KISAK_PLATFORM_DEFAULT)

if (KISAK_PLATFORM STREQUAL "web")
    if (NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        message(FATAL_ERROR "KISAK_PLATFORM=web requires the Emscripten toolchain")
    endif()
elseif (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    message(FATAL_ERROR "The Emscripten toolchain must be configured with KISAK_PLATFORM=web")
elseif (NOT KISAK_PLATFORM STREQUAL "win32")
    message(FATAL_ERROR "Unsupported KISAK_PLATFORM: ${KISAK_PLATFORM}")
endif()

# Set Address Sanitizer
# NOTE: If you use this you will need to copy to the game's directory
# clang_rt.asan_dbg_dynamic-i386.dll and clang_rt.asan_dynamic-i386.dll from the MSVC C directory located below (or similar)
# C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\MSVC\14.44.35207\bin\Hostx86\x86
set(USE_ASAN OFF)

# Set to ON to build the sound system against OpenAL instead of Miles/MSS.
# When OFF (default), the original Miles Sound System (MSS/AIL) backend is used.
set(KISAK_OPENAL OFF)

# Set the gloabl directory variables
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(DEPS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps)
set(BIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(SCRIPTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/scripts)
set(PLATFORM_DIR ${SCRIPTS_DIR}/platform)

# Set the output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BIN_DIR})

# Include global scripts/tricks
include(${SCRIPTS_DIR}/extern/increment_build.cmake)

# Include platform overrides
include(${SCRIPTS_DIR}/platform_override.cmake)

# Log out the KISAK_PLATFORM
message("Building KISAK_PLATFORM: ${KISAK_PLATFORM}")

# The browser milestones remain intentionally isolated from the monolithic
# native targets. Those targets pull in Win32, Direct3D, Steam, Miles, and Bink
# before a platform override can replace them.
if (KISAK_PLATFORM STREQUAL "web")
    add_subdirectory(${SCRIPTS_DIR}/web)
else()
    add_subdirectory(${SCRIPTS_DIR}/mp)
    add_subdirectory(${SCRIPTS_DIR}/sp)
    add_subdirectory(${SCRIPTS_DIR}/dedi)
    add_subdirectory(${SCRIPTS_DIR}/radiant)
endif()
