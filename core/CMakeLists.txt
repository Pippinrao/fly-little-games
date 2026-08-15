cmake_minimum_required(VERSION 3.22)
project(nes_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NES_BUILD_TESTS "Build host-side tests" OFF)

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
target_compile_options(nestopia PRIVATE -Wno-narrowing)
# zlib is required by NstZlib.cpp (compressed save states / archives)
find_library(ZLIB_LIB z)
if(ZLIB_LIB)
    target_link_libraries(nestopia PUBLIC ${ZLIB_LIB})
else()
    message(WARNING "zlib not found; linking may fail on save-state compression")
endif()

# --- nes_abi: platform-independent C ABI implementation (placeholders for now) ---
add_library(nes_abi STATIC
    src/nes_core.cpp
    src/nes_stream.cpp
    src/nes_state.cpp
)
target_include_directories(nes_abi PUBLIC include)
target_link_libraries(nes_abi PUBLIC nestopia)

# --- host-side smoke test (not built for Android) ---
if(NES_BUILD_TESTS AND NOT ANDROID)
    enable_testing()
    add_executable(nes_core_test tests/test_core.cpp)
    target_link_libraries(nes_core_test PRIVATE nes_abi)
    add_test(NAME core_smoke COMMAND nes_core_test)
endif()
