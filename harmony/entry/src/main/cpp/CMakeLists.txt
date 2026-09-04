cmake_minimum_required(VERSION 3.22)

project(flynes_harmony_bridge LANGUAGES CXX)

set(FLYNES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
get_filename_component(FLYNES_REPOSITORY_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../.."
    ABSOLUTE
)
add_subdirectory(
    "${FLYNES_REPOSITORY_ROOT}/shared"
    "${CMAKE_CURRENT_BINARY_DIR}/flynes_shared"
)

add_library(entry SHARED
    napi_init.cpp
    catalog_smoke.cpp
)
target_compile_features(entry PRIVATE cxx_std_17)
set_target_properties(entry PROPERTIES
    CXX_EXTENSIONS NO
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
target_compile_options(entry PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
)
if(CMAKE_SYSTEM_NAME STREQUAL "OHOS" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(entry PRIVATE -Wno-error=unused-command-line-argument)
endif()
target_link_libraries(entry PRIVATE
    flynes_app
    libace_napi.z.so
)
target_link_options(entry PRIVATE
    -Wl,--no-undefined
    -Wl,--exclude-libs,ALL
)
