cmake_minimum_required(VERSION 3.22)
project(nescore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(games-frame-pacing REQUIRED CONFIG)

# Reuse the core's static library targets (nes_abi -> nestopia).
add_subdirectory(${NES_CORE_DIR} ${CMAKE_BINARY_DIR}/core)

if(NOT FLYNES_SHARED_DIR)
    message(FATAL_ERROR "FLYNES_SHARED_DIR is required to link flynes_app")
endif()
set(FLYNES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(${FLYNES_SHARED_DIR} ${CMAKE_BINARY_DIR}/flynes_shared)

add_library(nescore SHARED
    nes_jni.cpp
    flynes_app_jni.cpp
    nearby/session_owner.cpp
    nearby/nearby_mvp_jni.cpp
    nearby/content_port.cpp
    nearby/prepared_content.cpp
    nearby/content_jni.cpp
    nearby/crypto_port.cpp
    nearby/crypto_jni.cpp
    video/baseline_pipeline.cpp
    video/egl_presenter.cpp
    video/framebuffer_target.cpp
    video/gpu_timer_query.cpp
    video/mmpx_pass.cpp
    video/motion_compute_jni.cpp
    video/motion_compute_pipeline.cpp
    video/presentation_coordinator.cpp
    video/scalefx_pipeline.cpp
    video/shader_program.cpp
    video/spatial_offscreen_jni.cpp
    video/spatial_pipeline.cpp
    video/video_presenter_jni.cpp)
target_include_directories(nescore PRIVATE ${NES_CORE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}
    ${FLYNES_SHARED_DIR}/src/session)
target_link_libraries(nescore nes_abi nestopia video_algorithms flynes_app flynes_session
    flynes_session_codec)
if(TARGET flynes_runtime)
    target_link_libraries(nescore flynes_runtime flynes_product_dual_runtime)
endif()
if(TARGET flynes_nearby_quic_provider)
    target_link_libraries(nescore flynes_nearby_quic_provider)
    add_dependencies(nescore flynes_nearby_quic_provider_build)
    target_compile_definitions(nescore PRIVATE FLYNES_ENABLE_RUST_QUIC_PROVIDER=1)
endif()
if(TARGET flynes_lan_mvp)
    target_link_libraries(nescore flynes_lan_mvp)
else()
    message(FATAL_ERROR "Android nearby MVP requires the Rust QUIC provider")
endif()

# Safety net: zlib is required by NstZlib.cpp (compressed save states).
# It propagates PUBLIC through nestopia -> nes_abi, but link it explicitly
# in case the propagation ever changes.
find_library(ZLIB_LIB z)
if(ZLIB_LIB)
    target_link_libraries(nescore ${ZLIB_LIB})
endif()

# ANativeWindow_* (native_window.h) live in libandroid, which is not linked
# by default on Android.
find_library(ANDROID_LIB android)
if(ANDROID_LIB)
    target_link_libraries(nescore ${ANDROID_LIB})
endif()

# __android_log_print (android/log.h) lives in liblog.
find_library(LOG_LIB log)
if(LOG_LIB)
    target_link_libraries(nescore ${LOG_LIB})
endif()

find_library(EGL_LIB EGL)
find_library(GLES2_LIB GLESv2)
find_library(GLES3_LIB GLESv3)
if(NOT EGL_LIB OR NOT GLES2_LIB OR NOT GLES3_LIB)
    message(FATAL_ERROR "Native presenter requires EGL and GLESv2")
endif()
target_link_libraries(nescore games-frame-pacing::swappy_static
    ${EGL_LIB} ${GLES2_LIB} ${GLES3_LIB})

find_library(DL_LIB dl)
if(DL_LIB)
    target_link_libraries(nescore ${DL_LIB})
endif()
