cmake_minimum_required(VERSION 3.22)

project(flynes_harmony_bridge LANGUAGES CXX)

set(FLYNES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
get_filename_component(FLYNES_REPOSITORY_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../.."
    ABSOLUTE
)
add_subdirectory(
    "${FLYNES_REPOSITORY_ROOT}/core"
    "${CMAKE_CURRENT_BINARY_DIR}/nes_core"
)

# The Android and Harmony renderers compile the same pinned shader sources. The
# generated header stays in the build tree so there is one reviewed source and
# one algorithm hash for both products.
file(READ "${FLYNES_REPOSITORY_ROOT}/app/src/main/assets/shaders/mmpx/mmpx_2x.frag"
    FLYNES_MMPX_SHADER)
foreach(FLYNES_SCALEFX_PASS RANGE 0 4)
    file(READ
        "${FLYNES_REPOSITORY_ROOT}/app/src/main/assets/shaders/scalefx/scalefx-pass${FLYNES_SCALEFX_PASS}.glsl"
        FLYNES_SCALEFX_${FLYNES_SCALEFX_PASS})
endforeach()
configure_file(shader_assets.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/shader_assets.hpp" @ONLY)
add_subdirectory(
    "${FLYNES_REPOSITORY_ROOT}/shared"
    "${CMAKE_CURRENT_BINARY_DIR}/flynes_shared"
)

add_library(entry SHARED
    napi_init.cpp
    catalog_smoke.cpp
    play_session.cpp
    native_play_support.cpp
    native_play_runtime.cpp
    play_package.cpp
    product_bridge.cpp
    scan_job_queue.cpp
    scan_job_executor.cpp
    render_mailbox.cpp
    display_policy.cpp
    motion_frame_scheduler.cpp
    harmony_renderer.cpp
    harmony_spatial_pipeline.cpp
    "${FLYNES_REPOSITORY_ROOT}/app/src/main/cpp/video/motion_compute_pipeline.cpp"
)
target_include_directories(entry PRIVATE
    "${FLYNES_REPOSITORY_ROOT}/shared/src"
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    "${FLYNES_REPOSITORY_ROOT}/app/src/main/cpp/video"
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
    flynes_product
    flynes_runtime
    libace_napi.z.so
    libace_ndk.z.so
    libEGL.so
    libGLESv3.so
    libnative_vsync.so
    libnative_display_manager.so
    libohaudio.so
)
target_link_options(entry PRIVATE
    -Wl,--no-undefined
    -Wl,--exclude-libs,ALL
)
