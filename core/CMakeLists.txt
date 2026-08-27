cmake_minimum_required(VERSION 3.22)
project(nes_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NES_BUILD_TESTS "Build host-side tests" OFF)

add_library(video_algorithms STATIC
    video/temporal_interpolator.cpp
    video/temporal_scheduler.cpp
)
target_include_directories(video_algorithms PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set(NESTOPIA_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/vendor/nestopiaue/source/core)

# --- nestopia: upstream emulator core (299 .cpp, C++17) ---
file(GLOB_RECURSE NESTOPIA_SOURCES ${NESTOPIA_ROOT}/*.cpp)
add_library(nestopia STATIC ${NESTOPIA_SOURCES})
target_include_directories(nestopia PUBLIC
    ${NESTOPIA_ROOT}
    ${NESTOPIA_ROOT}/api
)
# NestopiaUE predates C++11 list-init narrowing rules: e.g. NstVideoFilterHqX.cpp
# narrows `WIDTH * sizeof(...)` (64-bit size_t on arm64) into a uint braced-init
# list, which clang rejects as an error on 64-bit targets. Downgrade to a warning
# (works on both clang and gcc); do not edit the vendored sources.
# MSVC rejects the flag outright, so guard for clang/gcc only.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(nestopia PRIVATE -Wno-narrowing)
endif()
# zlib is required by NstZlib.cpp (compressed save states / archives).
if(ANDROID)
    find_library(ZLIB_LIB z)
    if(NOT ZLIB_LIB)
        message(FATAL_ERROR "Android NDK zlib was not found")
    endif()
    target_link_libraries(nestopia PUBLIC ${ZLIB_LIB})
else()
    if(NOT DEFINED ZLIB_ROOT OR ZLIB_ROOT STREQUAL "")
        message(FATAL_ERROR "Host builds require ZLIB_ROOT as the verified installed prefix")
    endif()
    get_filename_component(ZLIB_PREFIX "${ZLIB_ROOT}" ABSOLUTE)
    find_path(FLYNES_ZLIB_INCLUDE zlib.h PATHS "${ZLIB_PREFIX}/include" NO_DEFAULT_PATH)
    find_library(FLYNES_ZLIB_STATIC NAMES zlibstatic
        PATHS "${ZLIB_PREFIX}/lib" NO_DEFAULT_PATH)
    if(NOT FLYNES_ZLIB_INCLUDE OR NOT FLYNES_ZLIB_STATIC)
        message(FATAL_ERROR "Verified zlib headers/static Release library are missing under ZLIB_ROOT")
    endif()
    file(READ "${FLYNES_ZLIB_INCLUDE}/zlib.h" FLYNES_ZLIB_HEADER_TEXT)
    if(NOT FLYNES_ZLIB_HEADER_TEXT MATCHES "#define ZLIB_VERSION \\\"1\\.3\\.1\\\"")
        message(FATAL_ERROR "ZLIB_ROOT is not the pinned zlib 1.3.1 install")
    endif()
    file(SHA256 "${FLYNES_ZLIB_INCLUDE}/zlib.h" FLYNES_ZLIB_HEADER_SHA256)
    file(SHA256 "${FLYNES_ZLIB_STATIC}" FLYNES_ZLIB_LIBRARY_SHA256)
    if(NOT FLYNES_ZLIB_HEADER_SHA256 STREQUAL
            "8a5579af72ea4f427ff00a4150f0ccb3fc5c1e4379f726e101133b1ab9fc600c")
        message(FATAL_ERROR "Pinned zlib.h SHA-256 mismatch")
    endif()
    if(NOT FLYNES_ZLIB_LIBRARY_SHA256 STREQUAL
            "64ebc5489d54d283af93a614c48c82443f9cf146b04db728c57a008f4a4f3a45")
        message(FATAL_ERROR "Pinned zlibstatic.lib SHA-256 mismatch")
    endif()
    add_library(flynes_zlib STATIC IMPORTED GLOBAL)
    set_target_properties(flynes_zlib PROPERTIES
        IMPORTED_LOCATION "${FLYNES_ZLIB_STATIC}"
        INTERFACE_INCLUDE_DIRECTORIES "${FLYNES_ZLIB_INCLUDE}")
    target_link_libraries(nestopia PUBLIC flynes_zlib)
endif()

# --- nes_abi: platform-independent C ABI implementation (placeholders for now) ---
add_library(nes_abi STATIC
    src/nes_core.cpp
    src/nes_stream.cpp
    src/nes_state.cpp
)
# Static ABI: on Windows the header would otherwise mark every symbol
# __declspec(dllimport) (MSVC rejects dllimport definitions). NES_STATIC is a
# no-op on non-Windows (Android) builds. PUBLIC so the host test inherits it.
target_compile_definitions(nes_abi PUBLIC NES_STATIC)
target_include_directories(nes_abi PUBLIC include)
target_link_libraries(nes_abi PUBLIC nestopia)

# --- host-side smoke test (not built for Android) ---
if(NES_BUILD_TESTS AND NOT ANDROID)
    enable_testing()
    add_executable(nes_core_test tests/test_core.cpp)
    target_link_libraries(nes_core_test PRIVATE nes_abi)
    add_test(NAME core_smoke COMMAND nes_core_test)
    # ctest runs with the build dir as CWD; the test's default ROM path
    # ("core/tests/fixtures/from_below.nes") is relative to the repo root,
    # so anchor the test's working directory there.
    set_tests_properties(core_smoke PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..)

    add_executable(temporal_interpolator_test tests/test_temporal_interpolator.cpp)
    target_link_libraries(temporal_interpolator_test PRIVATE video_algorithms)
    add_test(NAME temporal_interpolator COMMAND temporal_interpolator_test)
endif()
