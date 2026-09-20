cmake_minimum_required(VERSION 3.22)

project(flynes_shared LANGUAGES CXX)

option(FLYNES_BUILD_TESTS "Build FlyNES shared-layer host tests" OFF)

add_library(flynes_app STATIC
    src/catalog/game_title_index.cpp
    src/catalog/bounded_zip_archive.cpp
    src/catalog/content_identity.cpp
    src/catalog/rom_payload_parser.cpp
    src/catalog/unsupported_payload_classifier.cpp
    src/app/catalog_persist.cpp
    src/app/settings_persist.cpp
    src/app/layout_persist.cpp
    src/app/flynes_app.cpp
)
target_include_directories(flynes_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(flynes_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(flynes_app PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(flynes_app PRIVATE /utf-8)
endif()
set_target_properties(flynes_app PROPERTIES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE YES
)

# Payload inflation uses only platform/toolchain zlib. Windows host evidence is pinned to the
# same verified 1.3.1 static package as core; cross targets resolve inside their SDK sysroots.
if(WIN32)
    if(NOT DEFINED ZLIB_ROOT OR ZLIB_ROOT STREQUAL "")
        message(FATAL_ERROR
            "Windows shared host builds require ZLIB_ROOT as the verified installed prefix")
    endif()
    get_filename_component(FLYNES_SHARED_ZLIB_PREFIX "${ZLIB_ROOT}" ABSOLUTE)
    find_path(FLYNES_SHARED_ZLIB_INCLUDE zlib.h
        PATHS "${FLYNES_SHARED_ZLIB_PREFIX}/include" NO_DEFAULT_PATH)
    find_library(FLYNES_SHARED_ZLIB_STATIC NAMES zlibstatic
        PATHS "${FLYNES_SHARED_ZLIB_PREFIX}/lib" NO_DEFAULT_PATH)
    if(NOT FLYNES_SHARED_ZLIB_INCLUDE OR NOT FLYNES_SHARED_ZLIB_STATIC)
        message(FATAL_ERROR
            "Verified zlib headers/static Release library are missing under ZLIB_ROOT")
    endif()
    file(READ "${FLYNES_SHARED_ZLIB_INCLUDE}/zlib.h" FLYNES_SHARED_ZLIB_HEADER_TEXT)
    if(NOT FLYNES_SHARED_ZLIB_HEADER_TEXT MATCHES
            "#define ZLIB_VERSION \\\"1\\.3\\.1\\\"")
        message(FATAL_ERROR "ZLIB_ROOT is not the pinned zlib 1.3.1 install")
    endif()
    file(SHA256 "${FLYNES_SHARED_ZLIB_INCLUDE}/zlib.h"
        FLYNES_SHARED_ZLIB_HEADER_SHA256)
    file(SHA256 "${FLYNES_SHARED_ZLIB_STATIC}"
        FLYNES_SHARED_ZLIB_LIBRARY_SHA256)
    if(NOT FLYNES_SHARED_ZLIB_HEADER_SHA256 STREQUAL
            "8a5579af72ea4f427ff00a4150f0ccb3fc5c1e4379f726e101133b1ab9fc600c")
        message(FATAL_ERROR "Pinned zlib.h SHA-256 mismatch")
    endif()
    if(NOT FLYNES_SHARED_ZLIB_LIBRARY_SHA256 STREQUAL
            "9f2898c18ee6e982ec5266ddcbd4cd8d068e9d0b6c3122f6ecb49d8be3e8d9ff")
        message(FATAL_ERROR "Pinned zlibstatic.lib SHA-256 mismatch")
    endif()
    add_library(flynes_shared_zlib STATIC IMPORTED GLOBAL)
    set_target_properties(flynes_shared_zlib PROPERTIES
        IMPORTED_LOCATION "${FLYNES_SHARED_ZLIB_STATIC}"
        INTERFACE_INCLUDE_DIRECTORIES "${FLYNES_SHARED_ZLIB_INCLUDE}")
    target_link_libraries(flynes_app PRIVATE flynes_shared_zlib)
elseif(APPLE AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
    if(NOT IS_ABSOLUTE "${CMAKE_OSX_SYSROOT}" OR
       NOT IS_DIRECTORY "${CMAKE_OSX_SYSROOT}")
        message(FATAL_ERROR
            "FLYNES_IOS shared builds require an absolute CMAKE_OSX_SYSROOT")
    endif()
    find_path(FLYNES_SHARED_IOS_ZLIB_INCLUDE zlib.h
        PATHS "${CMAKE_OSX_SYSROOT}/usr/include"
        NO_DEFAULT_PATH)
    find_library(FLYNES_SHARED_IOS_ZLIB_LIBRARY NAMES z
        PATHS "${CMAKE_OSX_SYSROOT}/usr/lib"
        NO_DEFAULT_PATH)
    if(NOT FLYNES_SHARED_IOS_ZLIB_INCLUDE OR NOT FLYNES_SHARED_IOS_ZLIB_LIBRARY)
        message(FATAL_ERROR
            "FLYNES_IOS shared could not find zlib inside the selected SDK")
    endif()

    file(REAL_PATH "${CMAKE_OSX_SYSROOT}" FLYNES_SHARED_IOS_SDK_REAL)
    foreach(FLYNES_SHARED_IOS_ZLIB_PATH IN ITEMS
            "${FLYNES_SHARED_IOS_ZLIB_INCLUDE}"
            "${FLYNES_SHARED_IOS_ZLIB_LIBRARY}")
        file(REAL_PATH "${FLYNES_SHARED_IOS_ZLIB_PATH}"
            FLYNES_SHARED_IOS_ZLIB_PATH_REAL)
        file(RELATIVE_PATH FLYNES_SHARED_IOS_ZLIB_RELATIVE
            "${FLYNES_SHARED_IOS_SDK_REAL}"
            "${FLYNES_SHARED_IOS_ZLIB_PATH_REAL}")
        if(IS_ABSOLUTE "${FLYNES_SHARED_IOS_ZLIB_RELATIVE}" OR
           FLYNES_SHARED_IOS_ZLIB_RELATIVE MATCHES "^\\.\\.($|/)")
            message(FATAL_ERROR "FLYNES_IOS shared zlib escaped the selected SDK")
        endif()
    endforeach()
    target_include_directories(flynes_app PRIVATE "${FLYNES_SHARED_IOS_ZLIB_INCLUDE}")
    target_link_libraries(flynes_app PRIVATE "${FLYNES_SHARED_IOS_ZLIB_LIBRARY}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/flynes-ios-shared-zlib.txt"
        "sdk=${FLYNES_SHARED_IOS_SDK_REAL}\n"
        "include=${FLYNES_SHARED_IOS_ZLIB_INCLUDE}\n"
        "library=${FLYNES_SHARED_IOS_ZLIB_LIBRARY}\n")
elseif(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    find_path(FLYNES_SHARED_ZLIB_INCLUDE zlib.h)
    find_library(FLYNES_SHARED_ZLIB_LIBRARY NAMES z)
    if(NOT FLYNES_SHARED_ZLIB_INCLUDE OR NOT FLYNES_SHARED_ZLIB_LIBRARY)
        message(FATAL_ERROR "The target SDK sysroot zlib was not found")
    endif()
    if(NOT CMAKE_SYSROOT)
        message(FATAL_ERROR "The target SDK did not define CMAKE_SYSROOT")
    endif()
    # HarmonyOS BiSheng overlays its HMS sysroot on the base OpenHarmony
    # sysroot and registers its usr directory as an official CMake find root.
    # Android and base OpenHarmony builds continue to trust only CMAKE_SYSROOT.
    set(FLYNES_SHARED_ALLOWED_SYSROOTS "${CMAKE_SYSROOT}")
    if(CMAKE_SYSTEM_NAME STREQUAL "OHOS" AND
       DEFINED HMOS_SDK_NATIVE AND
       IS_DIRECTORY "${HMOS_SDK_NATIVE}/sysroot/usr")
        file(REAL_PATH "${HMOS_SDK_NATIVE}/sysroot/usr"
            FLYNES_SHARED_HMOS_FIND_ROOT_REAL)
        set(FLYNES_SHARED_HMOS_OVERLAY_REGISTERED FALSE)
        foreach(FLYNES_SHARED_FIND_ROOT IN LISTS CMAKE_FIND_ROOT_PATH)
            if(IS_DIRECTORY "${FLYNES_SHARED_FIND_ROOT}")
                file(REAL_PATH "${FLYNES_SHARED_FIND_ROOT}"
                    FLYNES_SHARED_FIND_ROOT_REAL)
                if(FLYNES_SHARED_FIND_ROOT_REAL STREQUAL
                   FLYNES_SHARED_HMOS_FIND_ROOT_REAL)
                    set(FLYNES_SHARED_HMOS_OVERLAY_REGISTERED TRUE)
                    break()
                endif()
            endif()
        endforeach()
        if(FLYNES_SHARED_HMOS_OVERLAY_REGISTERED)
            list(APPEND FLYNES_SHARED_ALLOWED_SYSROOTS
                "${HMOS_SDK_NATIVE}/sysroot")
        endif()
    endif()
    foreach(FLYNES_SHARED_ZLIB_PATH IN ITEMS
            "${FLYNES_SHARED_ZLIB_INCLUDE}"
            "${FLYNES_SHARED_ZLIB_LIBRARY}")
        file(REAL_PATH "${FLYNES_SHARED_ZLIB_PATH}" FLYNES_SHARED_ZLIB_PATH_REAL)
        set(FLYNES_SHARED_ZLIB_PATH_ALLOWED FALSE)
        foreach(FLYNES_SHARED_ALLOWED_SYSROOT IN LISTS FLYNES_SHARED_ALLOWED_SYSROOTS)
            file(REAL_PATH "${FLYNES_SHARED_ALLOWED_SYSROOT}"
                FLYNES_SHARED_ALLOWED_SYSROOT_REAL)
            file(RELATIVE_PATH FLYNES_SHARED_ZLIB_PATH_RELATIVE
                "${FLYNES_SHARED_ALLOWED_SYSROOT_REAL}"
                "${FLYNES_SHARED_ZLIB_PATH_REAL}")
            if(NOT FLYNES_SHARED_ZLIB_PATH_RELATIVE MATCHES "^\\.\\.($|/)" AND
               NOT IS_ABSOLUTE "${FLYNES_SHARED_ZLIB_PATH_RELATIVE}")
                set(FLYNES_SHARED_ZLIB_PATH_ALLOWED TRUE)
                break()
            endif()
        endforeach()
        if(NOT FLYNES_SHARED_ZLIB_PATH_ALLOWED)
            message(FATAL_ERROR "The selected zlib escaped the target SDK sysroots")
        endif()
    endforeach()
    target_include_directories(flynes_app PRIVATE "${FLYNES_SHARED_ZLIB_INCLUDE}")
    target_link_libraries(flynes_app PRIVATE "${FLYNES_SHARED_ZLIB_LIBRARY}")
else()
    find_package(ZLIB REQUIRED)
    target_link_libraries(flynes_app PRIVATE ZLIB::ZLIB)
endif()

function(flynes_enable_strict_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /WX
            $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
        )
        if(CMAKE_SYSTEM_NAME STREQUAL "OHOS" AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${target} PRIVATE
                -Wno-error=unused-command-line-argument
            )
        endif()
    endif()
endfunction()

flynes_enable_strict_warnings(flynes_app)

add_library(flynes_product STATIC
    src/product/control_layout.cpp
    src/product/gamepad_hit_map.cpp
    src/product/game_center_state.cpp
    src/product/nearby_ui_state.cpp
    src/product/multiplayer_eligibility.cpp
)
target_include_directories(flynes_product PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(flynes_product PUBLIC cxx_std_17)
set_target_properties(flynes_product PROPERTIES CXX_EXTENSIONS NO POSITION_INDEPENDENT_CODE YES)
target_link_libraries(flynes_app PUBLIC flynes_product)

add_library(flynes_session_codec STATIC
    src/session/wire/sha256.cpp
    src/session/wire/session_codec.cpp
    src/session/wire/app_frame.cpp
    src/session/wire/pair_capability.cpp
    src/session/wire/gatt_lookup_v2.cpp
    src/session/wire/initial_bearer.cpp
    src/session/wire/endpoint_offer.cpp
    src/session/wire/gatt_fragment.cpp
    src/session/wire/app_stream_assembler.cpp
    src/session/wire/pair_crypto.cpp
    src/session/wire/pair_handshake.cpp
    src/session/wire/pair_reveal.cpp
    src/session/wire/pair_secure.cpp
    src/session/wire/p256_point.cpp
    src/session/wire/quic_stream_gate.cpp
    src/session/wire/prebind_record_queue.cpp
    src/session/wire/quic_contract.cpp
    src/session/wire/channel_bind.cpp
    src/session/wire/session_signing_binding.cpp
    src/session/wire/link_hello.cpp
    src/session/wire/link_ready.cpp
)
target_include_directories(flynes_session_codec PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/session
    ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(flynes_session_codec PRIVATE cxx_std_17)
set_target_properties(flynes_session_codec PROPERTIES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE YES
)
flynes_enable_strict_warnings(flynes_session_codec)

add_library(flynes_session_ports STATIC
    src/session/buffer_handle.cpp
    src/session/ports/provider_events.cpp
    src/session/ports/provider_operation_journal.cpp
)
target_include_directories(flynes_session_ports PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/session
)
target_compile_features(flynes_session_ports PUBLIC cxx_std_17)
set_target_properties(flynes_session_ports PROPERTIES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE YES
)
flynes_enable_strict_warnings(flynes_session_ports)

add_library(flynes_initial_plan_lock STATIC
    src/session/initial_plan_lock.cpp
    src/session/link_activation.cpp
    src/session/link/link_orchestrator.cpp
    src/session/link/initial_plan_scheduler.cpp
    src/session/link/initial_bearer_scheduler.cpp
    src/session/link/endpoint_offer_scheduler.cpp
    src/session/link/initial_quic_bind_scheduler.cpp
    src/session/link/pair_capability_scheduler.cpp
    src/session/link/pair_known_envelope_queue.cpp
    src/session/link/pair_known_scheduler.cpp
    src/session/link/pair_material_scheduler.cpp
    src/session/link/pair_key_confirm_scheduler.cpp
    src/session/link/pair_reveal_scheduler.cpp
    src/session/link/pair_sas_scheduler.cpp
    src/session/link/pair_signature_scheduler.cpp
    src/session/link/session_signing_scheduler.cpp
    src/session/link/link_handshake_scheduler.cpp
    src/session/link/pair_pipeline.cpp
    src/session/pair_auth_reducer.cpp
)
target_link_libraries(flynes_initial_plan_lock PUBLIC
    flynes_session_codec flynes_session_ports)
target_include_directories(flynes_initial_plan_lock PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
target_compile_features(flynes_initial_plan_lock PUBLIC cxx_std_17)
set_target_properties(flynes_initial_plan_lock PROPERTIES CXX_EXTENSIONS NO POSITION_INDEPENDENT_CODE YES)
flynes_enable_strict_warnings(flynes_initial_plan_lock)

add_library(flynes_invite_code_route STATIC
    src/session/invite_code_route.cpp
    src/session/invite_coordinator.cpp
)
target_link_libraries(flynes_invite_code_route PUBLIC flynes_session_codec)
target_include_directories(flynes_invite_code_route PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
target_compile_features(flynes_invite_code_route PUBLIC cxx_std_17)
set_target_properties(flynes_invite_code_route PROPERTIES CXX_EXTENSIONS NO POSITION_INDEPENDENT_CODE YES)
flynes_enable_strict_warnings(flynes_invite_code_route)

add_library(flynes_session STATIC
    src/product/dual_start_identity.cpp
    src/session/flynes_session.cpp
    src/session/flynes_session_v2.cpp
    src/session/engine/session_engine.cpp
    src/session/view/session_view.cpp
    src/session/dual/canonical_input.cpp
    src/session/dual/canonical_input_wire.cpp
    src/session/dual/input_window.cpp
    src/session/dual/dual_run_scheduler.cpp
    src/session/dual/dual_state_digest.cpp
    src/session/dual/dual_runtime_adapter.cpp
    src/session/dual/dual_session_controller.cpp
    src/session/content/content_offer_scheduler.cpp
    src/session/content/content_offer_wire.cpp
    src/session/content/content_transfer_controller.cpp
    src/session/recovery/link_activity_watchdog.cpp
    src/session/recovery/input_sequence_ledger.cpp
    src/session/recovery/durable_root_store.cpp
    src/session/recovery/recovery_reducer.cpp
)
target_include_directories(flynes_session PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(flynes_session PRIVATE cxx_std_17)
target_link_libraries(flynes_session PRIVATE
    flynes_initial_plan_lock flynes_invite_code_route flynes_session_ports)
set_target_properties(flynes_session PROPERTIES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE YES
)
flynes_enable_strict_warnings(flynes_session)

function(flynes_add_runtime_library)
    if(TARGET flynes_runtime)
        return()
    endif()
    if(NOT TARGET nes_abi)
        return()
    endif()
    add_library(flynes_runtime STATIC
        src/runtime/flynes_runtime.cpp
    )
    target_include_directories(flynes_runtime PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_compile_features(flynes_runtime PRIVATE cxx_std_17)
    set_target_properties(flynes_runtime PROPERTIES
        CXX_EXTENSIONS NO
        POSITION_INDEPENDENT_CODE YES
    )
    target_link_libraries(flynes_runtime PUBLIC nes_abi)
    flynes_enable_strict_warnings(flynes_runtime)
endfunction()

# ---------------------------------------------------------------------------
# Nearby QUIC provider (Rust / Quinn static library).
#
# The provider is the repository's only real QUIC transport implementation. It
# is a Rust staticlib exposing a C ABI (see nearby-quic-provider/include/
# flynes_quic_provider.h), so it cannot go through add_subdirectory. Test
# targets that need a genuine loopback QUIC connection link the imported target
# below and additionally depend on the custom target that runs cargo.
#
# Until 2026-09-15 nothing in shared/CMakeLists.txt referenced the provider at
# all, which is why no CTest suite had ever exercised a real QUIC connection.
# ---------------------------------------------------------------------------
option(FLYNES_ENABLE_RUST_QUIC_PROVIDER
    "Build and link the Rust nearby QUIC provider (requires cargo)" ON)

if(FLYNES_ENABLE_RUST_QUIC_PROVIDER)
    find_program(FLYNES_CARGO_EXECUTABLE cargo
        HINTS "$ENV{HOME}/.cargo/bin" "$ENV{USERPROFILE}/.cargo/bin")
    if(NOT FLYNES_CARGO_EXECUTABLE)
        message(STATUS
            "nearby QUIC provider: cargo not found; the provider target is disabled")
        set(FLYNES_ENABLE_RUST_QUIC_PROVIDER OFF CACHE BOOL "" FORCE)
    endif()
endif()

if(FLYNES_ENABLE_RUST_QUIC_PROVIDER)
    set(FLYNES_QUIC_PROVIDER_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/nearby-quic-provider")
    set(FLYNES_QUIC_CARGO_ARGS build --release
        --manifest-path "${FLYNES_QUIC_PROVIDER_ROOT}/Cargo.toml")
    file(GLOB_RECURSE FLYNES_QUIC_PROVIDER_SOURCES
        "${FLYNES_QUIC_PROVIDER_ROOT}/src/*.rs")
    list(APPEND FLYNES_QUIC_PROVIDER_SOURCES
        "${FLYNES_QUIC_PROVIDER_ROOT}/Cargo.toml")
    if(ANDROID)
        if(ANDROID_ABI STREQUAL "arm64-v8a")
            set(FLYNES_QUIC_RUST_TARGET aarch64-linux-android)
        elseif(ANDROID_ABI STREQUAL "x86_64")
            set(FLYNES_QUIC_RUST_TARGET x86_64-linux-android)
        else()
            message(FATAL_ERROR
                "nearby QUIC provider: unsupported ANDROID_ABI ${ANDROID_ABI}")
        endif()
        if(CMAKE_HOST_WIN32)
            set(FLYNES_NDK_LLVM_HOST windows-x86_64)
            set(FLYNES_NDK_CLANG_EXT ".cmd")
        elseif(CMAKE_HOST_APPLE)
            set(FLYNES_NDK_LLVM_HOST darwin-x86_64)
            set(FLYNES_NDK_CLANG_EXT "")
        else()
            set(FLYNES_NDK_LLVM_HOST linux-x86_64)
            set(FLYNES_NDK_CLANG_EXT "")
        endif()
        set(FLYNES_NDK_LLVM
            "${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/${FLYNES_NDK_LLVM_HOST}/bin")
        set(FLYNES_ANDROID_API "${ANDROID_NATIVE_API_LEVEL}")
        string(REPLACE "android-" "" FLYNES_ANDROID_API "${FLYNES_ANDROID_API}")
        set(FLYNES_NDK_CC
            "${FLYNES_NDK_LLVM}/${FLYNES_QUIC_RUST_TARGET}${FLYNES_ANDROID_API}-clang${FLYNES_NDK_CLANG_EXT}")
        set(FLYNES_NDK_AR "${FLYNES_NDK_LLVM}/llvm-ar.exe")
        if(NOT CMAKE_HOST_WIN32)
            set(FLYNES_NDK_AR "${FLYNES_NDK_LLVM}/llvm-ar")
        endif()
        string(REPLACE "-" "_" FLYNES_QUIC_TARGET_ENV "${FLYNES_QUIC_RUST_TARGET}")
        string(TOUPPER "${FLYNES_QUIC_TARGET_ENV}" FLYNES_QUIC_TARGET_ENV)
        set(FLYNES_QUIC_PROVIDER_LIBRARY
            "${FLYNES_QUIC_PROVIDER_ROOT}/target/${FLYNES_QUIC_RUST_TARGET}/release/libflynes_nearby_quic_provider.a")
        list(APPEND FLYNES_QUIC_CARGO_ARGS --target "${FLYNES_QUIC_RUST_TARGET}")
        add_custom_command(
            OUTPUT "${FLYNES_QUIC_PROVIDER_LIBRARY}"
            COMMAND "${CMAKE_COMMAND}" -E env
                "ANDROID_NDK_HOME=${CMAKE_ANDROID_NDK}"
                "CC_${FLYNES_QUIC_RUST_TARGET}=${FLYNES_NDK_CC}"
                "AR_${FLYNES_QUIC_RUST_TARGET}=${FLYNES_NDK_AR}"
                "CARGO_TARGET_${FLYNES_QUIC_TARGET_ENV}_LINKER=${FLYNES_NDK_CC}"
                "${FLYNES_CARGO_EXECUTABLE}" ${FLYNES_QUIC_CARGO_ARGS}
            WORKING_DIRECTORY "${FLYNES_QUIC_PROVIDER_ROOT}"
            DEPENDS ${FLYNES_QUIC_PROVIDER_SOURCES}
            COMMENT "cargo build --release --target ${FLYNES_QUIC_RUST_TARGET} (nearby QUIC)"
            VERBATIM)
    elseif(WIN32)
        set(FLYNES_QUIC_PROVIDER_LIBRARY
            "${FLYNES_QUIC_PROVIDER_ROOT}/target/release/flynes_nearby_quic_provider.lib")
        add_custom_command(
            OUTPUT "${FLYNES_QUIC_PROVIDER_LIBRARY}"
            COMMAND "${FLYNES_CARGO_EXECUTABLE}" ${FLYNES_QUIC_CARGO_ARGS}
            WORKING_DIRECTORY "${FLYNES_QUIC_PROVIDER_ROOT}"
            DEPENDS ${FLYNES_QUIC_PROVIDER_SOURCES}
            COMMENT "cargo build --release (flynes nearby QUIC provider)"
            VERBATIM)
    else()
        set(FLYNES_QUIC_PROVIDER_LIBRARY
            "${FLYNES_QUIC_PROVIDER_ROOT}/target/release/libflynes_nearby_quic_provider.a")
        add_custom_command(
            OUTPUT "${FLYNES_QUIC_PROVIDER_LIBRARY}"
            COMMAND "${FLYNES_CARGO_EXECUTABLE}" ${FLYNES_QUIC_CARGO_ARGS}
            WORKING_DIRECTORY "${FLYNES_QUIC_PROVIDER_ROOT}"
            DEPENDS ${FLYNES_QUIC_PROVIDER_SOURCES}
            COMMENT "cargo build --release (flynes nearby QUIC provider)"
            VERBATIM)
    endif()

    add_custom_target(flynes_nearby_quic_provider_build
        DEPENDS "${FLYNES_QUIC_PROVIDER_LIBRARY}")

    add_library(flynes_nearby_quic_provider STATIC IMPORTED GLOBAL)
    set_target_properties(flynes_nearby_quic_provider PROPERTIES
        IMPORTED_LOCATION "${FLYNES_QUIC_PROVIDER_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FLYNES_QUIC_PROVIDER_ROOT}/include")
    if(ANDROID)
        set_property(TARGET flynes_nearby_quic_provider APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES log)
    elseif(WIN32)
        # A Rust staticlib on MSVC still needs the platform libraries its
        # dependencies (ring, rustls, socket2, getrandom) reference.
        set_property(TARGET flynes_nearby_quic_provider APPEND PROPERTY
            INTERFACE_LINK_LIBRARIES
            "ws2_32;bcrypt;ntdll;userenv;crypt32;advapi32;ole32;shell32;iphlpapi;user32")
    endif()
    message(STATUS "nearby QUIC provider: linked from ${FLYNES_QUIC_PROVIDER_LIBRARY}")
endif()

if(FLYNES_BUILD_TESTS)
    enable_language(C)
    enable_testing()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    # GNU-compatible ELF --wrap is test observation only, unavailable on Apple/MSVC.
    if(UNIX AND NOT APPLE AND NOT ANDROID)
        find_package(Threads REQUIRED)
        add_executable(flynes_android_crypto_port_test
            tests/nearby/integration/test_android_crypto_port.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/nearby/crypto_port.cpp)
        target_link_libraries(flynes_android_crypto_port_test PRIVATE flynes_session_ports Threads::Threads)
        target_include_directories(flynes_android_crypto_port_test PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src/session
            ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/nearby)
        target_compile_features(flynes_android_crypto_port_test PRIVATE cxx_std_17)
        set_target_properties(flynes_android_crypto_port_test PROPERTIES CXX_EXTENSIONS NO)
        flynes_enable_strict_warnings(flynes_android_crypto_port_test)
        target_link_options(flynes_android_crypto_port_test PRIVATE
            -Wl,--wrap=fly_session_inbox_retain_v2 -Wl,--wrap=fly_session_inbox_release_v2
            -Wl,--wrap=fly_session_deliver_v2)
        add_test(NAME flynes_android_crypto_port COMMAND flynes_android_crypto_port_test)
        set_tests_properties(flynes_android_crypto_port PROPERTIES TIMEOUT 60 LABELS "nearby_integration;nearby_contract")

        add_executable(flynes_android_crypto_ownership_test
            tests/nearby/integration/test_android_crypto_ownership.cpp
            tests/nearby/harness/two_engine_loopback_fixture.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/nearby/crypto_port.cpp)
        target_link_libraries(flynes_android_crypto_ownership_test PRIVATE flynes_session Threads::Threads)
        target_include_directories(flynes_android_crypto_ownership_test PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src/session
            ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/nearby/harness
            ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/nearby)
        target_compile_features(flynes_android_crypto_ownership_test PRIVATE cxx_std_17)
        set_target_properties(flynes_android_crypto_ownership_test PROPERTIES CXX_EXTENSIONS NO)
        flynes_enable_strict_warnings(flynes_android_crypto_ownership_test)
        target_link_options(flynes_android_crypto_ownership_test PRIVATE
            -Wl,--wrap=fly_session_create_v2 -Wl,--wrap=fly_session_deliver_v2)
        add_test(NAME flynes_android_crypto_ownership COMMAND flynes_android_crypto_ownership_test)
        set_tests_properties(flynes_android_crypto_ownership PROPERTIES TIMEOUT 60 LABELS "nearby_integration;nearby_contract")
    endif()

    add_test(NAME flynes_game_title_data_check
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../tools/game_titles/game_titles.py check)
    add_test(NAME flynes_game_title_generator
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/../tools/game_titles/test_game_titles.py)

    if(NOT TARGET nes_abi)
        set(NES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../core"
                         "${CMAKE_CURRENT_BINARY_DIR}/nes_core")
    endif()
    flynes_add_runtime_library()

    add_executable(flynes_product_dual_runtime_port_test
        tests/nearby/integration/test_product_dual_runtime_port.cpp)
    target_link_libraries(flynes_product_dual_runtime_port_test PRIVATE flynes_product_dual_runtime)
    target_include_directories(flynes_product_dual_runtime_port_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_product_dual_runtime_port_test PRIVATE cxx_std_17)
    target_compile_definitions(flynes_product_dual_runtime_port_test PRIVATE
        FLYNES_RUNTIME_ROM_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../content/assets/roms/thwaite.nes"
        FLYNES_RUNTIME_OTHER_ROM_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../content/assets/roms/twin_dragons.nes")
    set_target_properties(flynes_product_dual_runtime_port_test PROPERTIES CXX_EXTENSIONS NO)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_definitions(flynes_product_dual_runtime_port_test PRIVATE FLYNES_TEST_WRAP_ALLOC)
        target_link_options(flynes_product_dual_runtime_port_test PRIVATE
            "-Wl,--wrap=calloc" "-Wl,--wrap=_Znwm" "-Wl,--wrap=nes_load_rom")
    endif()
    flynes_enable_strict_warnings(flynes_product_dual_runtime_port_test)
    add_test(NAME flynes_product_dual_runtime_port COMMAND flynes_product_dual_runtime_port_test)
    set_tests_properties(flynes_product_dual_runtime_port PROPERTIES LABELS "nearby_integration;runtime;product")

    add_executable(flynes_runtime_test
        tests/test_runtime.cpp
        src/session/wire/sha256.cpp
    )
    target_link_libraries(flynes_runtime_test PRIVATE flynes_runtime)
    target_compile_features(flynes_runtime_test PRIVATE cxx_std_17)
    target_compile_definitions(flynes_runtime_test PRIVATE
        FLYNES_RUNTIME_ROM_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../content/assets/roms/thwaite.nes"
        FLYNES_RUNTIME_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/include/flynes/flynes_runtime.h"
    )
    set_target_properties(flynes_runtime_test PROPERTIES CXX_EXTENSIONS NO)
    if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_definitions(flynes_runtime_test PRIVATE FLYNES_TEST_WRAP_SOURCE_INFO)
        target_link_options(flynes_runtime_test PRIVATE "-Wl,--wrap=nes_get_rom_info")
    endif()
    flynes_enable_strict_warnings(flynes_runtime_test)
    add_test(NAME flynes_runtime COMMAND flynes_runtime_test)

    find_package(Threads REQUIRED)
    add_executable(flynes_runtime_pcm_contention_test tests/test_runtime_pcm_contention.cpp)
    target_include_directories(flynes_runtime_pcm_contention_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(flynes_runtime_pcm_contention_test PRIVATE nes_abi Threads::Threads)
    target_compile_features(flynes_runtime_pcm_contention_test PRIVATE cxx_std_17)
    set_target_properties(flynes_runtime_pcm_contention_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_runtime_pcm_contention_test)
    add_test(NAME flynes_runtime_pcm_contention COMMAND flynes_runtime_pcm_contention_test)
    set_tests_properties(flynes_runtime_pcm_contention PROPERTIES TIMEOUT 15)

    set(FLYNES_ANDROID_NDK_ROOT "" CACHE PATH
        "Android NDK root for the cross-sysroot policy regression test")
    set(FLYNES_ANDROID_POLICY_NINJA "" CACHE FILEPATH
        "Ninja executable for the Android cross-sysroot policy regression test")
    if(FLYNES_ANDROID_NDK_ROOT)
        set(FLYNES_ANDROID_POLICY_TOOLCHAIN
            "${FLYNES_ANDROID_NDK_ROOT}/build/cmake/android.toolchain.cmake")
        if(NOT EXISTS "${FLYNES_ANDROID_POLICY_TOOLCHAIN}")
            message(FATAL_ERROR
                "FLYNES_ANDROID_NDK_ROOT does not contain android.toolchain.cmake")
        endif()
        if(NOT EXISTS "${FLYNES_ANDROID_POLICY_NINJA}")
            message(FATAL_ERROR
                "FLYNES_ANDROID_POLICY_NINJA must name an existing Ninja executable")
        endif()
        add_test(
            NAME flynes_android_hmos_overlay_policy
            COMMAND
                "${CMAKE_COMMAND}"
                "-DFLYNES_TEST_SHARED_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
                "-DFLYNES_TEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/android-hmos-overlay-policy"
                "-DFLYNES_TEST_ANDROID_TOOLCHAIN_FILE=${FLYNES_ANDROID_POLICY_TOOLCHAIN}"
                "-DFLYNES_TEST_MAKE_PROGRAM=${FLYNES_ANDROID_POLICY_NINJA}"
                -P
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_android_hmos_overlay_policy.cmake"
        )
    endif()

    add_executable(flynes_game_title_index_test tests/test_game_title_index.cpp)
    target_link_libraries(flynes_game_title_index_test PRIVATE flynes_app)
    target_compile_features(flynes_game_title_index_test PRIVATE cxx_std_17)
    flynes_enable_strict_warnings(flynes_game_title_index_test)
    if(MSVC)
        target_compile_options(flynes_game_title_index_test PRIVATE /utf-8)
    endif()
    add_test(NAME flynes_game_title_index COMMAND flynes_game_title_index_test)

    add_executable(flynes_app_contract_test
        tests/test_app.cpp
    )
    target_link_libraries(flynes_app_contract_test PRIVATE flynes_app)
    target_compile_features(flynes_app_contract_test PRIVATE cxx_std_17)
    set_target_properties(flynes_app_contract_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_app_contract_test)
    add_test(NAME flynes_app_contract COMMAND flynes_app_contract_test)

    add_executable(flynes_product_game_center_test tests/test_product_game_center.cpp)
    target_link_libraries(flynes_product_game_center_test PRIVATE flynes_product)
    flynes_enable_strict_warnings(flynes_product_game_center_test)
    add_test(NAME flynes_product_game_center COMMAND flynes_product_game_center_test)

    add_executable(flynes_product_control_layout_test tests/test_product_control_layout.cpp)
    target_link_libraries(flynes_product_control_layout_test PRIVATE flynes_product)
    flynes_enable_strict_warnings(flynes_product_control_layout_test)
    add_test(NAME flynes_product_control_layout COMMAND flynes_product_control_layout_test)

    add_executable(flynes_product_gamepad_hit_map_test tests/test_product_gamepad_hit_map.cpp)
    target_link_libraries(flynes_product_gamepad_hit_map_test PRIVATE flynes_product)
    flynes_enable_strict_warnings(flynes_product_gamepad_hit_map_test)
    add_test(NAME flynes_product_gamepad_hit_map COMMAND flynes_product_gamepad_hit_map_test)

    add_executable(flynes_product_pause_actions_test tests/test_product_pause_actions.cpp)
    target_link_libraries(flynes_product_pause_actions_test PRIVATE flynes_product)
    flynes_enable_strict_warnings(flynes_product_pause_actions_test)
    add_test(NAME flynes_product_pause_actions COMMAND flynes_product_pause_actions_test)

    add_executable(flynes_product_nearby_ui_test tests/test_product_nearby_ui.cpp)
    target_link_libraries(flynes_product_nearby_ui_test PRIVATE flynes_product)
    flynes_enable_strict_warnings(flynes_product_nearby_ui_test)
    add_test(NAME nearby_product_ui COMMAND flynes_product_nearby_ui_test)

    add_executable(flynes_multiplayer_eligibility_test tests/test_multiplayer_eligibility.cpp)
    target_link_libraries(flynes_multiplayer_eligibility_test PRIVATE flynes_product)
    flynes_enable_strict_warnings(flynes_multiplayer_eligibility_test)
    add_test(NAME flynes_multiplayer_eligibility COMMAND flynes_multiplayer_eligibility_test)

    add_executable(flynes_catalog_scan_test
        tests/test_catalog_scan.cpp
    )
    target_link_libraries(flynes_catalog_scan_test PRIVATE flynes_app)
    target_compile_features(flynes_catalog_scan_test PRIVATE cxx_std_17)
    set_target_properties(flynes_catalog_scan_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_catalog_scan_test)
    add_test(NAME flynes_catalog_scan COMMAND flynes_catalog_scan_test)

    add_executable(flynes_catalog_persist_test
        tests/test_catalog_persist.cpp
    )
    target_link_libraries(flynes_catalog_persist_test PRIVATE flynes_app)
    target_include_directories(flynes_catalog_persist_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_features(flynes_catalog_persist_test PRIVATE cxx_std_17)
    set_target_properties(flynes_catalog_persist_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_catalog_persist_test)
    add_test(NAME flynes_catalog_persist COMMAND flynes_catalog_persist_test)

    add_executable(flynes_catalog_user_test
        tests/test_catalog_user.cpp
    )
    target_link_libraries(flynes_catalog_user_test PRIVATE flynes_app)
    target_compile_features(flynes_catalog_user_test PRIVATE cxx_std_17)
    set_target_properties(flynes_catalog_user_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_catalog_user_test)
    add_test(NAME flynes_catalog_user COMMAND flynes_catalog_user_test)

    add_executable(flynes_catalog_source_remove_test
        tests/test_catalog_source_remove.cpp
    )
    target_link_libraries(flynes_catalog_source_remove_test PRIVATE flynes_app)
    target_compile_features(flynes_catalog_source_remove_test PRIVATE cxx_std_17)
    set_target_properties(flynes_catalog_source_remove_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_catalog_source_remove_test)
    add_test(NAME flynes_catalog_source_remove COMMAND flynes_catalog_source_remove_test)

    add_executable(flynes_settings_test
        tests/test_settings.cpp
    )
    target_link_libraries(flynes_settings_test PRIVATE flynes_app)
    target_compile_features(flynes_settings_test PRIVATE cxx_std_17)
    set_target_properties(flynes_settings_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_settings_test)
    add_test(NAME flynes_settings COMMAND flynes_settings_test)

    add_executable(flynes_control_layout_persist_test
        tests/test_control_layout_persist.cpp
    )
    target_link_libraries(flynes_control_layout_persist_test PRIVATE flynes_app flynes_product)
    target_compile_features(flynes_control_layout_persist_test PRIVATE cxx_std_17)
    set_target_properties(flynes_control_layout_persist_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_control_layout_persist_test)
    add_test(NAME flynes_control_layout_persist COMMAND flynes_control_layout_persist_test)

    add_executable(flynes_c_header_test
        tests/test_c_header.c
    )
    target_link_libraries(flynes_c_header_test PRIVATE flynes_app)
    set_target_properties(flynes_c_header_test PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
        LINKER_LANGUAGE CXX
    )
    flynes_enable_strict_warnings(flynes_c_header_test)
    add_test(NAME flynes_c_header COMMAND flynes_c_header_test)

    add_executable(flynes_rom_payload_parser_test
        tests/rom_fixture.cpp
        tests/test_rom_payload_parser.cpp
    )
    target_link_libraries(flynes_rom_payload_parser_test PRIVATE flynes_app)
    target_include_directories(flynes_rom_payload_parser_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(flynes_rom_payload_parser_test PRIVATE
        FLYNES_ROM_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rom/v1"
    )
    target_compile_features(flynes_rom_payload_parser_test PRIVATE cxx_std_17)
    set_target_properties(flynes_rom_payload_parser_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_rom_payload_parser_test)
    add_test(NAME flynes_rom_payload_parser COMMAND flynes_rom_payload_parser_test)

    add_executable(flynes_content_identity_test
        tests/identity_fixture.cpp
        tests/test_content_identity.cpp
    )
    target_link_libraries(flynes_content_identity_test PRIVATE flynes_app)
    target_include_directories(flynes_content_identity_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(flynes_content_identity_test PRIVATE
        FLYNES_IDENTITY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/catalog/identity/v1"
    )
    target_compile_features(flynes_content_identity_test PRIVATE cxx_std_17)
    set_target_properties(flynes_content_identity_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_content_identity_test)
    add_test(NAME flynes_content_identity COMMAND flynes_content_identity_test)

    add_executable(flynes_identity_fixture_loader_test
        tests/identity_fixture.cpp
        tests/test_identity_fixture_loader.cpp
    )
    target_link_libraries(flynes_identity_fixture_loader_test PRIVATE flynes_app)
    target_include_directories(flynes_identity_fixture_loader_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(flynes_identity_fixture_loader_test PRIVATE
        FLYNES_IDENTITY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/catalog/identity/v1"
        FLYNES_IDENTITY_FIXTURE_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/identity-fixture-loader-tests"
    )
    target_compile_features(flynes_identity_fixture_loader_test PRIVATE cxx_std_17)
    set_target_properties(flynes_identity_fixture_loader_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_identity_fixture_loader_test)
    add_test(NAME flynes_identity_fixture_loader COMMAND flynes_identity_fixture_loader_test)

    add_executable(flynes_unsupported_payload_classifier_test
        tests/unsupported_payload_fixture.cpp
        tests/test_unsupported_payload_classifier.cpp
    )
    target_link_libraries(flynes_unsupported_payload_classifier_test PRIVATE flynes_app)
    target_include_directories(flynes_unsupported_payload_classifier_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(flynes_unsupported_payload_classifier_test PRIVATE
        FLYNES_UNSUPPORTED_PAYLOAD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/catalog/unsupported_payload/v1"
    )
    target_compile_features(flynes_unsupported_payload_classifier_test PRIVATE cxx_std_17)
    set_target_properties(flynes_unsupported_payload_classifier_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_unsupported_payload_classifier_test)
    add_test(NAME flynes_unsupported_payload_classifier
        COMMAND flynes_unsupported_payload_classifier_test)

    add_executable(flynes_unsupported_payload_fixture_loader_test
        tests/unsupported_payload_fixture.cpp
        tests/test_unsupported_payload_fixture_loader.cpp
    )
    target_link_libraries(flynes_unsupported_payload_fixture_loader_test PRIVATE flynes_app)
    target_include_directories(flynes_unsupported_payload_fixture_loader_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(flynes_unsupported_payload_fixture_loader_test PRIVATE
        FLYNES_UNSUPPORTED_PAYLOAD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/catalog/unsupported_payload/v1"
        FLYNES_UNSUPPORTED_PAYLOAD_FIXTURE_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/unsupported-payload-fixture-loader-tests"
    )
    target_compile_features(flynes_unsupported_payload_fixture_loader_test PRIVATE cxx_std_17)
    set_target_properties(flynes_unsupported_payload_fixture_loader_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_unsupported_payload_fixture_loader_test)
    add_test(NAME flynes_unsupported_payload_fixture_loader
        COMMAND flynes_unsupported_payload_fixture_loader_test)

    add_executable(flynes_rom_fixture_loader_test
        tests/rom_fixture.cpp
        tests/test_rom_fixture_loader.cpp
    )
    target_link_libraries(flynes_rom_fixture_loader_test PRIVATE flynes_app)
    target_include_directories(flynes_rom_fixture_loader_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(flynes_rom_fixture_loader_test PRIVATE
        FLYNES_ROM_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rom/v1"
        FLYNES_ROM_FIXTURE_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/rom-fixture-loader-tests"
    )
    target_compile_features(flynes_rom_fixture_loader_test PRIVATE cxx_std_17)
    set_target_properties(flynes_rom_fixture_loader_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_rom_fixture_loader_test)
    add_test(NAME flynes_rom_fixture_loader COMMAND flynes_rom_fixture_loader_test)

    add_executable(flynes_bounded_zip_archive_test
        tests/zip_open_fixture.cpp
        tests/test_bounded_zip_archive.cpp
    )
    target_link_libraries(flynes_bounded_zip_archive_test PRIVATE flynes_app)
    target_include_directories(flynes_bounded_zip_archive_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(flynes_bounded_zip_archive_test PRIVATE
        FLYNES_ZIP_OPEN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/zip/open/v1"
    )
    target_compile_features(flynes_bounded_zip_archive_test PRIVATE cxx_std_17)
    set_target_properties(flynes_bounded_zip_archive_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_bounded_zip_archive_test)
    add_test(NAME flynes_bounded_zip_archive COMMAND flynes_bounded_zip_archive_test)

    add_executable(flynes_bounded_zip_archive_edges_test
        tests/test_bounded_zip_archive_edges.cpp
    )
    target_link_libraries(flynes_bounded_zip_archive_edges_test PRIVATE flynes_app)
    target_include_directories(flynes_bounded_zip_archive_edges_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_compile_features(flynes_bounded_zip_archive_edges_test PRIVATE cxx_std_17)
    set_target_properties(flynes_bounded_zip_archive_edges_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_bounded_zip_archive_edges_test)
    add_test(NAME flynes_bounded_zip_archive_edges COMMAND flynes_bounded_zip_archive_edges_test)

    add_executable(flynes_bounded_zip_payload_test
        tests/zip_payload_fixture.cpp
        tests/test_bounded_zip_payload.cpp
    )
    target_link_libraries(flynes_bounded_zip_payload_test PRIVATE flynes_app)
    target_include_directories(flynes_bounded_zip_payload_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(flynes_bounded_zip_payload_test PRIVATE
        FLYNES_ZIP_PAYLOAD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/zip/payload/v1"
    )
    target_compile_features(flynes_bounded_zip_payload_test PRIVATE cxx_std_17)
    set_target_properties(flynes_bounded_zip_payload_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_bounded_zip_payload_test)
    add_test(NAME flynes_bounded_zip_payload COMMAND flynes_bounded_zip_payload_test)

    add_executable(flynes_bounded_zip_payload_edges_test
        tests/zip_payload_fixture.cpp
        tests/test_bounded_zip_payload_edges.cpp
    )
    target_link_libraries(flynes_bounded_zip_payload_edges_test PRIVATE flynes_app)
    target_include_directories(flynes_bounded_zip_payload_edges_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(flynes_bounded_zip_payload_edges_test PRIVATE
        FLYNES_ZIP_PAYLOAD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/zip/payload/v1"
    )
    target_compile_features(flynes_bounded_zip_payload_edges_test PRIVATE cxx_std_17)
    set_target_properties(flynes_bounded_zip_payload_edges_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_bounded_zip_payload_edges_test)
    add_test(NAME flynes_bounded_zip_payload_edges COMMAND flynes_bounded_zip_payload_edges_test)

    add_executable(flynes_zip_payload_fixture_loader_test
        tests/zip_payload_fixture.cpp
        tests/test_zip_payload_fixture_loader.cpp
    )
    target_include_directories(flynes_zip_payload_fixture_loader_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(flynes_zip_payload_fixture_loader_test PRIVATE
        FLYNES_ZIP_PAYLOAD_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/zip/payload/v1"
        FLYNES_ZIP_PAYLOAD_FIXTURE_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/zip-payload-fixture-loader-tests"
    )
    target_compile_features(flynes_zip_payload_fixture_loader_test PRIVATE cxx_std_17)
    set_target_properties(flynes_zip_payload_fixture_loader_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_zip_payload_fixture_loader_test)
    add_test(NAME flynes_zip_payload_fixture_loader COMMAND flynes_zip_payload_fixture_loader_test)

    add_executable(flynes_zip_open_fixture_loader_test
        tests/zip_open_fixture.cpp
        tests/test_zip_open_fixture_loader.cpp
    )
    target_include_directories(flynes_zip_open_fixture_loader_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_compile_definitions(flynes_zip_open_fixture_loader_test PRIVATE
        FLYNES_ZIP_OPEN_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/zip/open/v1"
        FLYNES_ZIP_OPEN_FIXTURE_TEST_DIR="${CMAKE_CURRENT_BINARY_DIR}/zip-open-fixture-loader-tests"
    )
    target_compile_features(flynes_zip_open_fixture_loader_test PRIVATE cxx_std_17)
    set_target_properties(flynes_zip_open_fixture_loader_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_zip_open_fixture_loader_test)
    add_test(NAME flynes_zip_open_fixture_loader COMMAND flynes_zip_open_fixture_loader_test)

    add_test(
        NAME flynes_rom_fixture_generator
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rom_fixture_generator.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rom/generate_v1.py
            ${CMAKE_CURRENT_BINARY_DIR}/rom-fixture-generator-tests
    )

    add_test(
        NAME flynes_zip_open_fixture_generator
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_zip_open_fixture_generator.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/zip/open/generate_v1.py
            ${CMAKE_CURRENT_BINARY_DIR}/zip-open-fixture-generator-tests
    )

    add_test(
        NAME flynes_zip_open_fixture_corpus_check
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/zip/open/generate_v1.py
            --check
    )

    add_test(
        NAME flynes_zip_payload_fixture_generator
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_zip_payload_fixture_generator.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/zip/payload/generate_v1.py
            ${CMAKE_CURRENT_BINARY_DIR}/zip-payload-fixture-generator-tests
    )

    add_test(
        NAME flynes_zip_payload_fixture_corpus_check
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/zip/payload/generate_v1.py
            --check
    )

    add_test(
        NAME flynes_identity_fixture_corpus_check
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/catalog/identity/generate_v1.py
            --check
    )

    add_test(
        NAME flynes_identity_fixture_generator
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_identity_fixture_generator.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/catalog/identity/generate_v1.py
            ${CMAKE_CURRENT_BINARY_DIR}/identity-fixture-generator-tests
    )

    add_test(
        NAME flynes_unsupported_payload_fixture_corpus_check
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/catalog/unsupported_payload/generate_v1.py
            --check
    )

    add_test(
        NAME flynes_unsupported_payload_fixture_generator
        COMMAND
            ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_unsupported_payload_fixture_generator.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/catalog/unsupported_payload/generate_v1.py
            ${CMAKE_CURRENT_BINARY_DIR}/unsupported-payload-fixture-generator-tests
    )

    add_executable(flynes_session_schema_registry_test
        tests/test_session_schema_registry.cpp
    )
    target_compile_features(flynes_session_schema_registry_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_schema_registry_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_schema_registry_test)
    target_include_directories(flynes_session_schema_registry_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/schema
    )
    target_compile_definitions(flynes_session_schema_registry_test PRIVATE
        FLYNES_SESSION_SCHEMA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/schema"
        FLYNES_ANDROID_SESSION_SCHEMA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../app/src/test/resources/flynes_session_v1"
        FLYNES_IOS_SESSION_SCHEMA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../ios/abi/flynes_session_v1"
        FLYNES_HARMONY_SESSION_SCHEMA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../harmony/schema/flynes_session_v1"
    )
    add_test(NAME flynes_session_schema_registry
        COMMAND flynes_session_schema_registry_test)

    add_executable(flynes_session_codec_test
        tests/test_session_codec.cpp
    )
    target_link_libraries(flynes_session_codec_test PRIVATE flynes_session_codec)
    target_compile_features(flynes_session_codec_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_codec_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_codec_test)
    target_compile_definitions(flynes_session_codec_test PRIVATE
        FLYNES_SESSION_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/schema/golden"
    )
    add_test(NAME flynes_session_codec COMMAND flynes_session_codec_test)

    add_executable(flynes_pair_capability_test tests/test_pair_capability.cpp)
    target_link_libraries(flynes_pair_capability_test PRIVATE flynes_session_codec)
    target_compile_features(flynes_pair_capability_test PRIVATE cxx_std_17)
    set_target_properties(flynes_pair_capability_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_pair_capability_test)
    add_test(NAME flynes_pair_capability COMMAND flynes_pair_capability_test)

    add_executable(flynes_gatt_lookup_v2_test
        tests/nearby/protocol/test_gatt_lookup_v2.cpp
    )
    target_link_libraries(flynes_gatt_lookup_v2_test PRIVATE flynes_session_codec)
    target_compile_features(flynes_gatt_lookup_v2_test PRIVATE cxx_std_17)
    set_target_properties(flynes_gatt_lookup_v2_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_gatt_lookup_v2_test)
    target_compile_definitions(flynes_gatt_lookup_v2_test PRIVATE
        FLYNES_GATT_LOOKUP_V2_DIR="${CMAKE_CURRENT_SOURCE_DIR}/schema/gatt_lookup_v2"
    )
    add_test(NAME flynes_gatt_lookup_v2 COMMAND flynes_gatt_lookup_v2_test)
    set_tests_properties(flynes_gatt_lookup_v2 PROPERTIES LABELS "nearby_protocol")

    add_executable(flynes_gatt_fragment_test
        tests/nearby/protocol/test_gatt_fragment.cpp
    )
    target_link_libraries(flynes_gatt_fragment_test PRIVATE flynes_session_codec)
    target_compile_features(flynes_gatt_fragment_test PRIVATE cxx_std_17)
    set_target_properties(flynes_gatt_fragment_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_gatt_fragment_test)
    add_test(NAME flynes_gatt_fragment COMMAND flynes_gatt_fragment_test)
    set_tests_properties(flynes_gatt_fragment PROPERTIES LABELS "nearby_protocol")

    add_executable(flynes_invite_coordinator_test
        tests/nearby/protocol/test_invite_coordinator.cpp
    )
    target_link_libraries(flynes_invite_coordinator_test PRIVATE flynes_invite_code_route)
    target_compile_features(flynes_invite_coordinator_test PRIVATE cxx_std_17)
    set_target_properties(flynes_invite_coordinator_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_invite_coordinator_test)
    add_test(NAME flynes_invite_coordinator COMMAND flynes_invite_coordinator_test)
    set_tests_properties(flynes_invite_coordinator PROPERTIES LABELS "nearby_protocol")

    add_executable(flynes_app_stream_assembler_test
        tests/nearby/protocol/test_app_stream_assembler.cpp
    )
    target_link_libraries(flynes_app_stream_assembler_test PRIVATE flynes_session_codec)
    target_compile_features(flynes_app_stream_assembler_test PRIVATE cxx_std_17)
    set_target_properties(flynes_app_stream_assembler_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_app_stream_assembler_test)
    target_compile_definitions(flynes_app_stream_assembler_test PRIVATE
        FLYNES_SESSION_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/schema/golden"
    )
    add_test(NAME flynes_app_stream_assembler COMMAND flynes_app_stream_assembler_test)
    set_tests_properties(flynes_app_stream_assembler PROPERTIES LABELS "nearby_protocol")

    add_executable(flynes_pair_crypto_test
        tests/nearby/protocol/test_pair_crypto.cpp
    )
    target_link_libraries(flynes_pair_crypto_test PRIVATE flynes_session_codec)
    target_compile_features(flynes_pair_crypto_test PRIVATE cxx_std_17)
    set_target_properties(flynes_pair_crypto_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_pair_crypto_test)
    add_test(NAME flynes_pair_crypto COMMAND flynes_pair_crypto_test)
    set_tests_properties(flynes_pair_crypto PROPERTIES LABELS "nearby_protocol")

    add_executable(flynes_pair_handshake_test
        tests/nearby/protocol/test_pair_handshake.cpp
    )
    target_link_libraries(flynes_pair_handshake_test PRIVATE flynes_session_codec)
    target_compile_features(flynes_pair_handshake_test PRIVATE cxx_std_17)
    set_target_properties(flynes_pair_handshake_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_pair_handshake_test)
    add_test(NAME flynes_pair_handshake COMMAND flynes_pair_handshake_test)

    add_executable(flynes_initial_bearer_wire_test
        tests/nearby/protocol/test_initial_bearer_wire.cpp)
    target_link_libraries(flynes_initial_bearer_wire_test PRIVATE
        flynes_session_codec)
    target_compile_features(flynes_initial_bearer_wire_test PRIVATE cxx_std_17)
    set_target_properties(flynes_initial_bearer_wire_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_initial_bearer_wire_test)
    add_test(NAME flynes_initial_bearer_wire
        COMMAND flynes_initial_bearer_wire_test)

    add_executable(flynes_endpoint_offer_wire_test
        tests/nearby/protocol/test_endpoint_offer_wire.cpp)
    target_link_libraries(flynes_endpoint_offer_wire_test PRIVATE
        flynes_session_codec)
    target_compile_features(flynes_endpoint_offer_wire_test PRIVATE cxx_std_17)
    set_target_properties(flynes_endpoint_offer_wire_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_endpoint_offer_wire_test)
    add_test(NAME flynes_endpoint_offer_wire COMMAND flynes_endpoint_offer_wire_test)
    set_tests_properties(flynes_pair_handshake PROPERTIES LABELS "nearby_protocol")

    add_executable(flynes_p256_point_test
        tests/nearby/protocol/test_p256_point.cpp)
    target_link_libraries(flynes_p256_point_test PRIVATE flynes_session_codec)
    target_include_directories(flynes_p256_point_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire)
    target_compile_features(flynes_p256_point_test PRIVATE cxx_std_17)
    set_target_properties(flynes_p256_point_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_p256_point_test)
    add_test(NAME flynes_p256_point COMMAND flynes_p256_point_test)
    set_tests_properties(flynes_p256_point PROPERTIES LABELS "nearby_auth;nearby_protocol")

    add_executable(flynes_quic_stream_gate_test
        tests/nearby/protocol/test_quic_stream_gate.cpp
    )
    target_link_libraries(flynes_quic_stream_gate_test PRIVATE flynes_session_codec)
    target_compile_features(flynes_quic_stream_gate_test PRIVATE cxx_std_17)
    set_target_properties(flynes_quic_stream_gate_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_quic_stream_gate_test)
    add_test(NAME flynes_quic_stream_gate COMMAND flynes_quic_stream_gate_test)
    set_tests_properties(flynes_quic_stream_gate PROPERTIES LABELS "nearby_protocol")

    add_executable(flynes_prebind_record_queue_test
        tests/nearby/protocol/test_prebind_record_queue.cpp
    )
    target_link_libraries(flynes_prebind_record_queue_test PRIVATE flynes_session_codec)
    target_include_directories(flynes_prebind_record_queue_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
    )
    target_compile_features(flynes_prebind_record_queue_test PRIVATE cxx_std_17)
    set_target_properties(flynes_prebind_record_queue_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_prebind_record_queue_test)
    add_test(NAME flynes_prebind_record_queue COMMAND flynes_prebind_record_queue_test)
    set_tests_properties(flynes_prebind_record_queue PROPERTIES LABELS "nearby_protocol")

    add_executable(flynes_quic_contract_test
        tests/nearby/contract/test_quic_contract.cpp
    )
    target_link_libraries(flynes_quic_contract_test PRIVATE flynes_session_codec)
    target_compile_features(flynes_quic_contract_test PRIVATE cxx_std_17)
    set_target_properties(flynes_quic_contract_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_quic_contract_test)
    add_test(NAME flynes_quic_contract COMMAND flynes_quic_contract_test)
    set_tests_properties(flynes_quic_contract PROPERTIES LABELS "nearby_ports;nearby_protocol")

    add_executable(flynes_channel_bind_wire_test
        tests/nearby/protocol/test_channel_bind_wire.cpp)
    target_link_libraries(flynes_channel_bind_wire_test PRIVATE
        flynes_session_codec)
    target_compile_features(flynes_channel_bind_wire_test PRIVATE cxx_std_17)
    set_target_properties(flynes_channel_bind_wire_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_channel_bind_wire_test)
    add_test(NAME flynes_channel_bind_wire COMMAND flynes_channel_bind_wire_test)
    set_tests_properties(flynes_channel_bind_wire PROPERTIES LABELS "nearby_auth;nearby_protocol")

    add_executable(flynes_session_signing_binding_test
        tests/nearby/protocol/test_session_signing_binding.cpp)
    target_link_libraries(flynes_session_signing_binding_test PRIVATE
        flynes_session_codec)
    target_compile_features(flynes_session_signing_binding_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_signing_binding_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_signing_binding_test)
    add_test(NAME flynes_session_signing_binding
        COMMAND flynes_session_signing_binding_test)
    set_tests_properties(flynes_session_signing_binding PROPERTIES
        LABELS "nearby_auth;nearby_protocol")

    add_executable(flynes_initial_plan_lock_test tests/test_initial_plan_lock.cpp)
    target_link_libraries(flynes_initial_plan_lock_test PRIVATE flynes_initial_plan_lock)
    target_compile_features(flynes_initial_plan_lock_test PRIVATE cxx_std_17)
    set_target_properties(flynes_initial_plan_lock_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_initial_plan_lock_test)
    add_test(NAME flynes_initial_plan_lock COMMAND flynes_initial_plan_lock_test)

    add_executable(flynes_initial_plan_scheduler_test
        tests/nearby/integration/test_initial_plan_scheduler.cpp)
    target_link_libraries(flynes_initial_plan_scheduler_test PRIVATE
        flynes_initial_plan_lock)
    target_include_directories(flynes_initial_plan_scheduler_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_features(flynes_initial_plan_scheduler_test PRIVATE cxx_std_17)
    set_target_properties(flynes_initial_plan_scheduler_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_initial_plan_scheduler_test)
    add_test(NAME flynes_initial_plan_scheduler
        COMMAND flynes_initial_plan_scheduler_test)

    add_executable(flynes_initial_bearer_scheduler_test
        tests/nearby/integration/test_initial_bearer_scheduler.cpp)
    target_link_libraries(flynes_initial_bearer_scheduler_test PRIVATE
        flynes_initial_plan_lock)
    target_include_directories(flynes_initial_bearer_scheduler_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_features(flynes_initial_bearer_scheduler_test PRIVATE cxx_std_17)
    set_target_properties(flynes_initial_bearer_scheduler_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_initial_bearer_scheduler_test)
    add_test(NAME flynes_initial_bearer_scheduler
        COMMAND flynes_initial_bearer_scheduler_test)

    add_executable(flynes_initial_quic_bind_scheduler_test
        tests/nearby/integration/test_initial_quic_bind_scheduler.cpp)
    target_link_libraries(flynes_initial_quic_bind_scheduler_test PRIVATE
        flynes_initial_plan_lock)
    target_include_directories(flynes_initial_quic_bind_scheduler_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_features(flynes_initial_quic_bind_scheduler_test PRIVATE cxx_std_17)
    set_target_properties(flynes_initial_quic_bind_scheduler_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_initial_quic_bind_scheduler_test)
    add_test(NAME flynes_initial_quic_bind_scheduler
        COMMAND flynes_initial_quic_bind_scheduler_test)

    add_executable(flynes_pair_auth_reducer_test
        tests/nearby/protocol/test_pair_auth_reducer.cpp
    )
    target_link_libraries(flynes_pair_auth_reducer_test PRIVATE flynes_initial_plan_lock)
    target_compile_features(flynes_pair_auth_reducer_test PRIVATE cxx_std_17)
    set_target_properties(flynes_pair_auth_reducer_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_pair_auth_reducer_test)
    add_test(NAME flynes_pair_auth_reducer COMMAND flynes_pair_auth_reducer_test)
    set_tests_properties(flynes_pair_auth_reducer PROPERTIES LABELS "nearby_protocol;nearby_auth")

    add_executable(flynes_link_activation_test
        tests/nearby/protocol/test_link_activation.cpp
    )
    target_link_libraries(flynes_link_activation_test PRIVATE flynes_initial_plan_lock)
    target_include_directories(flynes_link_activation_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/nearby/harness)
    target_compile_features(flynes_link_activation_test PRIVATE cxx_std_17)
    set_target_properties(flynes_link_activation_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_link_activation_test)
    add_test(NAME flynes_link_activation COMMAND flynes_link_activation_test)
    set_tests_properties(flynes_link_activation PROPERTIES
        LABELS "nearby_protocol;nearby_auth"
    )

    add_executable(flynes_session_codec_loopback_test
        tests/test_session_codec_loopback.cpp
    )
    target_link_libraries(flynes_session_codec_loopback_test PRIVATE flynes_session_codec)
    if(WIN32)
        target_link_libraries(flynes_session_codec_loopback_test PRIVATE ws2_32)
    endif()
    target_compile_features(flynes_session_codec_loopback_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_codec_loopback_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_codec_loopback_test)
    target_compile_definitions(flynes_session_codec_loopback_test PRIVATE
        FLYNES_SESSION_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/schema/golden"
    )
    add_test(NAME flynes_session_codec_loopback COMMAND flynes_session_codec_loopback_test)

    add_executable(flynes_session_v2_abi_test
        tests/nearby/contract/test_session_v2_abi.c
    )
    target_link_libraries(flynes_session_v2_abi_test PRIVATE flynes_session)
    set_target_properties(flynes_session_v2_abi_test PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
    )
    flynes_enable_strict_warnings(flynes_session_v2_abi_test)
    add_test(NAME flynes_session_v2_abi COMMAND flynes_session_v2_abi_test)
    set_tests_properties(flynes_session_v2_abi PROPERTIES LABELS "nearby_abi")

    add_executable(flynes_session_v2_contract_test
        tests/nearby/contract/test_session_v2_contract.cpp
    )
    target_link_libraries(flynes_session_v2_contract_test PRIVATE flynes_session)
    target_include_directories(flynes_session_v2_contract_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_session_v2_contract_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_v2_contract_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_v2_contract_test)
    add_test(NAME flynes_session_v2_contract COMMAND flynes_session_v2_contract_test)
    set_tests_properties(flynes_session_v2_contract PROPERTIES
        LABELS "nearby_abi;nearby_ports"
    )

    add_executable(flynes_session_v2_buffer_test
        tests/nearby/contract/test_session_v2_buffer.cpp
    )
    target_link_libraries(flynes_session_v2_buffer_test PRIVATE flynes_session)
    target_include_directories(flynes_session_v2_buffer_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
    )
    target_compile_features(flynes_session_v2_buffer_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_v2_buffer_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_v2_buffer_test)
    add_test(NAME flynes_session_v2_buffer COMMAND flynes_session_v2_buffer_test)
    set_tests_properties(flynes_session_v2_buffer PROPERTIES
        LABELS "nearby_abi;nearby_ports"
    )

    add_executable(flynes_session_v2_provider_tables_test
        tests/nearby/contract/test_session_v2_provider_tables.cpp
    )
    target_link_libraries(flynes_session_v2_provider_tables_test PRIVATE flynes_session)
    target_compile_features(flynes_session_v2_provider_tables_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_v2_provider_tables_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_v2_provider_tables_test)
    add_test(NAME flynes_session_v2_provider_tables
        COMMAND flynes_session_v2_provider_tables_test)
    set_tests_properties(flynes_session_v2_provider_tables PROPERTIES
        LABELS "nearby_abi;nearby_ports")

    add_executable(flynes_provider_event_contract_test
        tests/nearby/contract/test_provider_event_contract.cpp
    )
    target_link_libraries(flynes_provider_event_contract_test PRIVATE flynes_session)
    target_include_directories(flynes_provider_event_contract_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
    )
    target_compile_features(flynes_provider_event_contract_test PRIVATE cxx_std_17)
    set_target_properties(flynes_provider_event_contract_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_provider_event_contract_test)
    add_test(NAME flynes_provider_event_contract
        COMMAND flynes_provider_event_contract_test)
    set_tests_properties(flynes_provider_event_contract PROPERTIES
        LABELS "nearby_abi;nearby_ports")

    add_executable(flynes_provider_operation_journal_test
        tests/nearby/contract/test_provider_operation_journal.cpp
    )
    target_link_libraries(flynes_provider_operation_journal_test PRIVATE flynes_session)
    target_include_directories(flynes_provider_operation_journal_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_provider_operation_journal_test PRIVATE cxx_std_17)
    set_target_properties(flynes_provider_operation_journal_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_provider_operation_journal_test)
    add_test(NAME flynes_provider_operation_journal
        COMMAND flynes_provider_operation_journal_test)
    set_tests_properties(flynes_provider_operation_journal PROPERTIES
        LABELS "nearby_auth;nearby_protocol;nearby_ports")

    add_executable(flynes_real_pair_pipeline_test
        tests/nearby/integration/test_real_pair_pipeline.cpp
    )
    target_link_libraries(flynes_real_pair_pipeline_test PRIVATE
        flynes_session flynes_initial_plan_lock)
    if(WIN32)
        target_link_libraries(flynes_real_pair_pipeline_test PRIVATE bcrypt)
    endif()
    target_include_directories(flynes_real_pair_pipeline_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_real_pair_pipeline_test PRIVATE cxx_std_17)
    set_target_properties(flynes_real_pair_pipeline_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_real_pair_pipeline_test)
    add_test(NAME flynes_real_pair_pipeline COMMAND flynes_real_pair_pipeline_test)
    set_tests_properties(flynes_real_pair_pipeline PROPERTIES
        LABELS "nearby_auth;nearby_protocol")

    add_executable(flynes_pair_material_scheduler_test
        tests/nearby/integration/test_pair_material_scheduler.cpp)
    target_link_libraries(flynes_pair_material_scheduler_test PRIVATE
        flynes_session flynes_initial_plan_lock)
    target_include_directories(flynes_pair_material_scheduler_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_pair_material_scheduler_test PRIVATE cxx_std_17)
    set_target_properties(flynes_pair_material_scheduler_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_pair_material_scheduler_test)
    add_test(NAME flynes_pair_material_scheduler
        COMMAND flynes_pair_material_scheduler_test)
    set_tests_properties(flynes_pair_material_scheduler PROPERTIES
        LABELS "nearby_auth;nearby_protocol;nearby_ports")

    add_executable(flynes_pair_known_scheduler_test
        tests/nearby/integration/test_pair_known_scheduler.cpp)
    target_link_libraries(flynes_pair_known_scheduler_test PRIVATE
        flynes_initial_plan_lock)
    target_include_directories(flynes_pair_known_scheduler_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_pair_known_scheduler_test PRIVATE cxx_std_17)
    set_target_properties(flynes_pair_known_scheduler_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_pair_known_scheduler_test)
    add_test(NAME flynes_pair_known_scheduler
        COMMAND flynes_pair_known_scheduler_test)
    set_tests_properties(flynes_pair_known_scheduler PROPERTIES
        LABELS "nearby_auth;nearby_protocol;nearby_ports")

    add_executable(flynes_session_signing_scheduler_test
        tests/nearby/integration/test_session_signing_scheduler.cpp)
    target_link_libraries(flynes_session_signing_scheduler_test PRIVATE
        flynes_initial_plan_lock)
    target_include_directories(flynes_session_signing_scheduler_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_session_signing_scheduler_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_signing_scheduler_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_signing_scheduler_test)
    add_test(NAME flynes_session_signing_scheduler
        COMMAND flynes_session_signing_scheduler_test)
    set_tests_properties(flynes_session_signing_scheduler PROPERTIES
        LABELS "nearby_auth;nearby_protocol;nearby_ports")

    add_executable(flynes_two_engine_empty_lobby_test
        tests/nearby/integration/test_two_engine_empty_lobby.cpp)
    target_link_libraries(flynes_two_engine_empty_lobby_test PRIVATE flynes_session)
    target_include_directories(flynes_two_engine_empty_lobby_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_two_engine_empty_lobby_test PRIVATE cxx_std_17)
    set_target_properties(flynes_two_engine_empty_lobby_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_two_engine_empty_lobby_test)
    add_test(NAME flynes_two_engine_empty_lobby
        COMMAND flynes_two_engine_empty_lobby_test)
    set_tests_properties(flynes_two_engine_empty_lobby PROPERTIES
        LABELS "nearby_integration;nearby_protocol")

    # W0-owned path-A acceptance target: two public engines over a byte-accurate
    # loopback. Grown in steps, and each step must keep this target green; see
    # tests/nearby/integration/test_two_engine_connected_lobby_e2e.cpp for the
    # step list. Deliberately separate from W3's two_engine_fixture/scenarios
    # names and from flynes_two_engine_empty_lobby_test, which stays as the
    # long-chain component regression.
    add_executable(flynes_two_engine_connected_lobby_e2e_test
        tests/nearby/integration/test_two_engine_connected_lobby_e2e.cpp
        tests/nearby/harness/two_engine_loopback_fixture.cpp)
    target_link_libraries(flynes_two_engine_connected_lobby_e2e_test PRIVATE flynes_session)
    target_include_directories(flynes_two_engine_connected_lobby_e2e_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/nearby/harness)
    target_compile_features(flynes_two_engine_connected_lobby_e2e_test PRIVATE cxx_std_17)
    set_target_properties(flynes_two_engine_connected_lobby_e2e_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_two_engine_connected_lobby_e2e_test)
    add_test(NAME flynes_two_engine_connected_lobby_e2e
        COMMAND flynes_two_engine_connected_lobby_e2e_test)
    set_tests_properties(flynes_two_engine_connected_lobby_e2e PROPERTIES
        LABELS "nearby_integration")

    add_executable(flynes_link_races_test
        tests/nearby/race/test_link_races.cpp)
    target_link_libraries(flynes_link_races_test PRIVATE flynes_initial_plan_lock)
    target_include_directories(flynes_link_races_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_link_races_test PRIVATE cxx_std_17)
    set_target_properties(flynes_link_races_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_link_races_test)
    add_test(NAME flynes_link_races COMMAND flynes_link_races_test)
    set_tests_properties(flynes_link_races PROPERTIES
        LABELS "nearby_race;nearby_protocol;nearby_auth")

    add_executable(flynes_pair_known_envelope_queue_test
        tests/nearby/race/test_pair_known_envelope_queue.cpp)
    target_link_libraries(flynes_pair_known_envelope_queue_test PRIVATE
        flynes_initial_plan_lock)
    target_include_directories(flynes_pair_known_envelope_queue_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_pair_known_envelope_queue_test PRIVATE cxx_std_17)
    set_target_properties(flynes_pair_known_envelope_queue_test PROPERTIES
        CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_pair_known_envelope_queue_test)
    add_test(NAME flynes_pair_known_envelope_queue
        COMMAND flynes_pair_known_envelope_queue_test)
    set_tests_properties(flynes_pair_known_envelope_queue PROPERTIES
        LABELS "nearby_race;nearby_protocol;nearby_auth")

    # W0 frozen contracts for the parallel DUAL worktrees (Task 2). These only
    # pin object kinds, exact sizes, discriminants, domains and pure gates; the
    # real codecs and schedulers are owned by W1/W2.
    add_executable(flynes_link_control_contract_test
        tests/nearby/contract/test_link_control_contract.cpp)
    target_link_libraries(flynes_link_control_contract_test PRIVATE
        flynes_session_codec)
    target_include_directories(flynes_link_control_contract_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_link_control_contract_test PRIVATE cxx_std_17)
    set_target_properties(flynes_link_control_contract_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_link_control_contract_test)
    add_test(NAME flynes_link_control_contract
        COMMAND flynes_link_control_contract_test)
    set_tests_properties(flynes_link_control_contract PROPERTIES
        LABELS "nearby_protocol;nearby_auth")

    add_executable(flynes_dual_runtime_contract_test
        tests/nearby/contract/test_dual_runtime_contract.cpp)
    target_link_libraries(flynes_dual_runtime_contract_test PRIVATE
        flynes_session_codec)
    target_include_directories(flynes_dual_runtime_contract_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_dual_runtime_contract_test PRIVATE cxx_std_17)
    set_target_properties(flynes_dual_runtime_contract_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_dual_runtime_contract_test)
    add_test(NAME flynes_dual_runtime_contract
        COMMAND flynes_dual_runtime_contract_test)
    set_tests_properties(flynes_dual_runtime_contract PROPERTIES
        LABELS "nearby_protocol;nearby_ports")

    # Proves that the Rust QUIC provider is actually built and linked into a
    # C++ test binary, so the two-engine harness can rely on real loopback QUIC
    # instead of a fake transport. Compiled only where cargo is available.
    if(FLYNES_ENABLE_RUST_QUIC_PROVIDER)
        add_executable(flynes_quic_provider_linkage_test
            tests/nearby/contract/test_quic_provider_linkage.cpp)
        target_link_libraries(flynes_quic_provider_linkage_test PRIVATE
            flynes_nearby_quic_provider)
        add_dependencies(flynes_quic_provider_linkage_test
            flynes_nearby_quic_provider_build)
        target_compile_features(flynes_quic_provider_linkage_test PRIVATE cxx_std_17)
        set_target_properties(flynes_quic_provider_linkage_test PROPERTIES CXX_EXTENSIONS NO)
        flynes_enable_strict_warnings(flynes_quic_provider_linkage_test)
        add_test(NAME flynes_quic_provider_linkage
            COMMAND flynes_quic_provider_linkage_test)
        set_tests_properties(flynes_quic_provider_linkage PROPERTIES
            LABELS "nearby_protocol;nearby_ports")
    endif()

    add_executable(flynes_session_v2_fencing_test
        tests/nearby/contract/test_session_v2_fencing.cpp
    )
    target_link_libraries(flynes_session_v2_fencing_test PRIVATE flynes_session)
    target_compile_features(flynes_session_v2_fencing_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_v2_fencing_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_v2_fencing_test)
    add_test(NAME flynes_session_v2_fencing COMMAND flynes_session_v2_fencing_test)
    set_tests_properties(flynes_session_v2_fencing PROPERTIES LABELS "nearby_ports")

    add_executable(flynes_session_test
        tests/test_session.cpp
        tests/test_session_header.c
    )
    target_link_libraries(flynes_session_test PRIVATE flynes_session)
    target_compile_features(flynes_session_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_test PROPERTIES
        CXX_EXTENSIONS NO
        LINKER_LANGUAGE CXX
    )
    flynes_enable_strict_warnings(flynes_session_test)
    add_test(NAME flynes_session COMMAND flynes_session_test)

    add_executable(flynes_session_initial_plan_test tests/test_session_initial_plan.cpp)
    target_link_libraries(flynes_session_initial_plan_test PRIVATE flynes_session flynes_session_codec)
    target_include_directories(flynes_session_initial_plan_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_session_initial_plan_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_initial_plan_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_initial_plan_test)
    add_test(NAME flynes_session_initial_plan COMMAND flynes_session_initial_plan_test)

    add_executable(flynes_session_public_path_test tests/test_session_public_path.cpp)
    target_link_libraries(flynes_session_public_path_test PRIVATE flynes_session flynes_session_codec)
    target_include_directories(flynes_session_public_path_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_session_public_path_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_public_path_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_public_path_test)
    target_compile_definitions(flynes_session_public_path_test PRIVATE
        FLYNES_SESSION_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/schema/golden"
    )
    add_test(NAME flynes_session_public_path COMMAND flynes_session_public_path_test)

    add_executable(flynes_invite_code_route_test tests/test_invite_code_route.cpp)
    target_link_libraries(flynes_invite_code_route_test PRIVATE flynes_invite_code_route flynes_session)
    flynes_enable_strict_warnings(flynes_invite_code_route_test)
    add_test(NAME flynes_invite_code_route COMMAND flynes_invite_code_route_test)


    add_executable(flynes_session_receive_seam_test tests/test_session_receive_seam.cpp)
    target_link_libraries(flynes_session_receive_seam_test PRIVATE flynes_session flynes_session_codec)
    target_include_directories(flynes_session_receive_seam_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_session_receive_seam_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_receive_seam_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_receive_seam_test)
    target_compile_definitions(flynes_session_receive_seam_test PRIVATE
        FLYNES_SESSION_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/schema/golden"
    )
    add_test(NAME flynes_session_receive_seam COMMAND flynes_session_receive_seam_test)

    add_executable(flynes_session_app_frame_test tests/test_app_frame.cpp)
    target_link_libraries(flynes_session_app_frame_test PRIVATE flynes_session_codec)
    target_compile_features(flynes_session_app_frame_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_app_frame_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_app_frame_test)
    target_compile_definitions(flynes_session_app_frame_test PRIVATE
        FLYNES_SESSION_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/schema/golden"
    )
    add_test(NAME flynes_session_app_frame COMMAND flynes_session_app_frame_test)

    add_executable(flynes_session_app_frame_consistency_test
        tests/test_app_frame_consistency.cpp
    )
    target_link_libraries(flynes_session_app_frame_consistency_test PRIVATE flynes_session_codec)
    target_compile_features(flynes_session_app_frame_consistency_test PRIVATE cxx_std_17)
    set_target_properties(flynes_session_app_frame_consistency_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_session_app_frame_consistency_test)
    target_compile_definitions(flynes_session_app_frame_consistency_test PRIVATE
        FLYNES_SESSION_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/schema/golden"
        FLYNES_SESSION_SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/schema/flynes_session_v1.schema"
    )
    add_test(NAME flynes_session_app_frame_consistency
        COMMAND flynes_session_app_frame_consistency_test)

    # ======================================================================
    # W1 (branch codex/nearby-dual-link-control) LINK_HELLO / LINK_READY.
    # Owner: W1. Task 3 = exact wire codecs (now translation units of the
    # permanent flynes_session_codec). Task 4 = link handshake scheduler (now a
    # translation unit of flynes_initial_plan_lock). The temporary
    # flynes_link_control_w1 container was deleted by W0 Task 9 once both
    # translation units were adopted by their permanent libraries.
    # ======================================================================
    add_executable(flynes_link_hello_wire_test
        tests/nearby/protocol/test_link_hello.cpp)
    target_link_libraries(flynes_link_hello_wire_test PRIVATE
        flynes_session_codec)
    target_include_directories(flynes_link_hello_wire_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_link_hello_wire_test PRIVATE cxx_std_17)
    set_target_properties(flynes_link_hello_wire_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_link_hello_wire_test)
    add_test(NAME flynes_link_hello_wire COMMAND flynes_link_hello_wire_test)
    set_tests_properties(flynes_link_hello_wire PROPERTIES
        LABELS "nearby_protocol;nearby_auth")

    add_executable(flynes_link_ready_wire_test
        tests/nearby/protocol/test_link_ready.cpp)
    target_link_libraries(flynes_link_ready_wire_test PRIVATE
        flynes_session_codec)
    target_include_directories(flynes_link_ready_wire_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_link_ready_wire_test PRIVATE cxx_std_17)
    set_target_properties(flynes_link_ready_wire_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_link_ready_wire_test)
    add_test(NAME flynes_link_ready_wire COMMAND flynes_link_ready_wire_test)
    set_tests_properties(flynes_link_ready_wire PROPERTIES
        LABELS "nearby_protocol;nearby_auth")

    add_executable(flynes_link_handshake_scheduler_test
        tests/nearby/integration/test_link_handshake_scheduler.cpp)
    target_link_libraries(flynes_link_handshake_scheduler_test PRIVATE
        flynes_initial_plan_lock)
    target_include_directories(flynes_link_handshake_scheduler_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_link_handshake_scheduler_test PRIVATE cxx_std_17)
    set_target_properties(flynes_link_handshake_scheduler_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_link_handshake_scheduler_test)
    add_test(NAME flynes_link_handshake_scheduler
        COMMAND flynes_link_handshake_scheduler_test)
    set_tests_properties(flynes_link_handshake_scheduler PROPERTIES
        LABELS "nearby_integration;nearby_protocol;nearby_ports")

    add_executable(flynes_link_handshake_races_test
        tests/nearby/race/test_link_handshake_races.cpp)
    target_link_libraries(flynes_link_handshake_races_test PRIVATE
        flynes_initial_plan_lock)
    target_include_directories(flynes_link_handshake_races_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_compile_features(flynes_link_handshake_races_test PRIVATE cxx_std_17)
    set_target_properties(flynes_link_handshake_races_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_link_handshake_races_test)
    add_test(NAME flynes_link_handshake_races
        COMMAND flynes_link_handshake_races_test)
    set_tests_properties(flynes_link_handshake_races PROPERTIES
        LABELS "nearby_race;nearby_protocol;nearby_ports")
endif()

# =====================================================================
# W2 (nearby-dual-runtime) append-only block. Owned by the
# codex/nearby-dual-runtime worker; the W0 merge applies it last. It adds new
# targets only and never edits an existing line above.
# =====================================================================
if(FLYNES_BUILD_TESTS)
    # W2 DUAL data plane sources, compiled directly into the W2 test targets so
    # that no pre-existing library line has to change.
    set(FLYNES_W2_DUAL_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual/canonical_input.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual/input_window.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual/dual_run_scheduler.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual/dual_state_digest.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire/sha256.cpp
    )
    set(FLYNES_W2_DUAL_INCLUDE_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual
    )

    add_executable(flynes_nearby_canonical_input_test
        tests/nearby/protocol/test_canonical_input.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual/canonical_input.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire/sha256.cpp
    )
    target_include_directories(flynes_nearby_canonical_input_test PRIVATE
        ${FLYNES_W2_DUAL_INCLUDE_DIRS})
    target_compile_features(flynes_nearby_canonical_input_test PRIVATE cxx_std_17)
    set_target_properties(flynes_nearby_canonical_input_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_nearby_canonical_input_test)
    add_test(NAME nearby_canonical_input COMMAND flynes_nearby_canonical_input_test)
    set_tests_properties(nearby_canonical_input PROPERTIES LABELS "nearby_protocol")

    add_executable(flynes_nearby_input_window_test
        tests/nearby/protocol/test_input_window.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual/canonical_input.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual/input_window.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire/sha256.cpp
    )
    target_include_directories(flynes_nearby_input_window_test PRIVATE
        ${FLYNES_W2_DUAL_INCLUDE_DIRS})
    target_compile_features(flynes_nearby_input_window_test PRIVATE cxx_std_17)
    set_target_properties(flynes_nearby_input_window_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_nearby_input_window_test)
    add_test(NAME nearby_input_window COMMAND flynes_nearby_input_window_test)
    set_tests_properties(nearby_input_window PROPERTIES LABELS "nearby_protocol")

    add_executable(flynes_nearby_dual_run_scheduler_test
        tests/nearby/integration/test_dual_run_scheduler.cpp
        ${FLYNES_W2_DUAL_SOURCES}
    )
    target_include_directories(flynes_nearby_dual_run_scheduler_test PRIVATE
        ${FLYNES_W2_DUAL_INCLUDE_DIRS})
    target_compile_features(flynes_nearby_dual_run_scheduler_test PRIVATE cxx_std_17)
    set_target_properties(flynes_nearby_dual_run_scheduler_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_nearby_dual_run_scheduler_test)
    add_test(NAME nearby_dual_run_scheduler COMMAND flynes_nearby_dual_run_scheduler_test)
    set_tests_properties(nearby_dual_run_scheduler PROPERTIES
        LABELS "nearby_integration;nearby_protocol")

    add_executable(flynes_nearby_dual_run_races_test
        tests/nearby/race/test_dual_run_races.cpp
        ${FLYNES_W2_DUAL_SOURCES}
    )
    target_include_directories(flynes_nearby_dual_run_races_test PRIVATE
        ${FLYNES_W2_DUAL_INCLUDE_DIRS})
    target_compile_features(flynes_nearby_dual_run_races_test PRIVATE cxx_std_17)
    set_target_properties(flynes_nearby_dual_run_races_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_nearby_dual_run_races_test)
    add_test(NAME nearby_dual_run_races COMMAND flynes_nearby_dual_run_races_test)
    set_tests_properties(nearby_dual_run_races PROPERTIES
        LABELS "nearby_race;nearby_integration")
endif()
# =================== end W2 append-only block =======================

# =====================================================================
# Task 10 (DUAL integration) append-only block.
# =====================================================================
if(FLYNES_BUILD_TESTS)
    # Step 3: the DUAL gate's own record that STREAM is closed explicitly.
    # Header-only assertions over the public ABI, the frozen link capability
    # contract and the frozen DUAL seam, so the target needs no library.
    add_executable(flynes_nearby_dual_stream_closed_test
        tests/nearby/contract/test_dual_stream_closed.cpp
    )
    target_include_directories(flynes_nearby_dual_stream_closed_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual
    )
    target_compile_features(flynes_nearby_dual_stream_closed_test PRIVATE cxx_std_17)
    set_target_properties(flynes_nearby_dual_stream_closed_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_nearby_dual_stream_closed_test)
    add_test(NAME nearby_dual_stream_closed
             COMMAND flynes_nearby_dual_stream_closed_test)
    set_tests_properties(nearby_dual_stream_closed PROPERTIES
        LABELS "nearby_contract")

    # Step 2 (first half): the canonical input unit on the wire (encoder and
    # decoder for the frozen 154-byte CanonicalInputBundleV1 record) and the
    # runtime port adapter that implements the frozen dual::DualRuntimePort over
    # the public C provider table. Both are asserted against the release's own
    # codec (flynes_session_codec supplies wire::check and the hash helpers).
    add_executable(flynes_nearby_dual_runtime_seam_test
        tests/nearby/integration/test_dual_runtime_seam.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual/canonical_input.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual/canonical_input_wire.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual/dual_runtime_adapter.cpp
    )
    target_include_directories(flynes_nearby_dual_runtime_seam_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual
    )
    target_compile_features(flynes_nearby_dual_runtime_seam_test PRIVATE cxx_std_17)
    set_target_properties(flynes_nearby_dual_runtime_seam_test PROPERTIES CXX_EXTENSIONS NO)
    target_link_libraries(flynes_nearby_dual_runtime_seam_test PRIVATE
        flynes_session_codec)
    flynes_enable_strict_warnings(flynes_nearby_dual_runtime_seam_test)
    add_test(NAME nearby_dual_runtime_seam
             COMMAND flynes_nearby_dual_runtime_seam_test)
    set_tests_properties(nearby_dual_runtime_seam PROPERTIES
        LABELS "nearby_integration;nearby_protocol")

    # Gap A+C: two public engines, content catalog, START_DUAL, 600-frame DUAL.
    add_executable(flynes_two_engine_dual_mvp_test
        tests/nearby/integration/test_two_engine_dual_mvp.cpp
        tests/nearby/harness/two_engine_loopback_fixture.cpp)
    target_link_libraries(flynes_two_engine_dual_mvp_test PRIVATE flynes_session)
    target_include_directories(flynes_two_engine_dual_mvp_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/nearby
    )
    target_compile_features(flynes_two_engine_dual_mvp_test PRIVATE cxx_std_17)
    set_target_properties(flynes_two_engine_dual_mvp_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_two_engine_dual_mvp_test)
    add_test(NAME flynes_two_engine_dual_mvp
             COMMAND flynes_two_engine_dual_mvp_test)
    set_tests_properties(flynes_two_engine_dual_mvp PROPERTIES
        LABELS "nearby_integration")

    add_executable(flynes_game_config_consent_test
        tests/nearby/integration/test_game_config_consent.cpp)
    target_link_libraries(flynes_game_config_consent_test PRIVATE flynes_session)
    target_include_directories(flynes_game_config_consent_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual
    )
    target_compile_features(flynes_game_config_consent_test PRIVATE cxx_std_17)
    set_target_properties(flynes_game_config_consent_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_game_config_consent_test)
    add_test(NAME flynes_game_config_consent
             COMMAND flynes_game_config_consent_test)
    set_tests_properties(flynes_game_config_consent PROPERTIES
        LABELS "nearby_integration;nearby_contract")

    if(TARGET flynes_runtime)
        add_executable(flynes_two_nes_schedulers_test
            tests/nearby/integration/test_two_nes_schedulers.cpp
            ${FLYNES_W2_DUAL_SOURCES})
        target_link_libraries(flynes_two_nes_schedulers_test PRIVATE flynes_runtime)
        target_include_directories(flynes_two_nes_schedulers_test PRIVATE
            ${FLYNES_W2_DUAL_INCLUDE_DIRS}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/nearby)
        target_compile_features(flynes_two_nes_schedulers_test PRIVATE cxx_std_17)
        target_compile_definitions(flynes_two_nes_schedulers_test PRIVATE
            FLYNES_RUNTIME_ROM_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../content/assets/roms/thwaite.nes")
        set_target_properties(flynes_two_nes_schedulers_test PROPERTIES CXX_EXTENSIONS NO)
        flynes_enable_strict_warnings(flynes_two_nes_schedulers_test)
        add_test(NAME flynes_two_nes_schedulers COMMAND flynes_two_nes_schedulers_test)
        set_tests_properties(flynes_two_nes_schedulers PROPERTIES
            LABELS "nearby_integration")

        add_executable(flynes_two_engine_nes_joint_test
            tests/nearby/integration/test_two_engine_nes_joint.cpp
            tests/nearby/harness/two_engine_loopback_fixture.cpp)
        target_link_libraries(flynes_two_engine_nes_joint_test PRIVATE
            flynes_session flynes_runtime)
        target_include_directories(flynes_two_engine_nes_joint_test PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src/session
            ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/nearby)
        target_compile_features(flynes_two_engine_nes_joint_test PRIVATE cxx_std_17)
        target_compile_definitions(flynes_two_engine_nes_joint_test PRIVATE
            FLYNES_RUNTIME_ROM_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../content/assets/roms/thwaite.nes")
        set_target_properties(flynes_two_engine_nes_joint_test PROPERTIES CXX_EXTENSIONS NO)
        flynes_enable_strict_warnings(flynes_two_engine_nes_joint_test)
        add_test(NAME flynes_two_engine_nes_joint
                 COMMAND flynes_two_engine_nes_joint_test)
        set_tests_properties(flynes_two_engine_nes_joint PROPERTIES
            LABELS "nearby_integration")

        if(FLYNES_ENABLE_RUST_QUIC_PROVIDER)
            add_executable(flynes_android_session_owner_test
                tests/nearby/integration/test_android_session_owner.cpp
                ../app/src/main/cpp/nearby/session_owner.cpp
                ../app/src/main/cpp/nearby/prepared_content.cpp
                ../app/src/main/cpp/nearby/content_port.cpp)
            target_link_libraries(flynes_android_session_owner_test PRIVATE
                flynes_session flynes_nearby_quic_provider flynes_product_dual_runtime)
            add_dependencies(flynes_android_session_owner_test flynes_nearby_quic_provider_build)
            target_include_directories(flynes_android_session_owner_test PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/nearby
                ${CMAKE_CURRENT_SOURCE_DIR}/src/session
                ${FLYNES_QUIC_PROVIDER_ROOT}/include)
            target_compile_features(flynes_android_session_owner_test PRIVATE cxx_std_17)
            target_compile_definitions(flynes_android_session_owner_test PRIVATE
                FLYNES_ENABLE_RUST_QUIC_PROVIDER=1 FLYNES_SESSION_OWNER_TEST_OBSERVER=1)
            flynes_enable_strict_warnings(flynes_android_session_owner_test)
            if(UNIX)
                target_link_libraries(flynes_android_session_owner_test PRIVATE pthread dl m)
            endif()
            add_test(NAME flynes_android_session_owner COMMAND flynes_android_session_owner_test)
            set_tests_properties(flynes_android_session_owner PROPERTIES TIMEOUT 60)

            add_executable(flynes_android_content_port_test
                tests/nearby/integration/test_android_content_port.cpp
                ../app/src/main/cpp/nearby/content_port.cpp)
            target_include_directories(flynes_android_content_port_test PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/nearby
                ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
            target_link_libraries(flynes_android_content_port_test PRIVATE flynes_session)
            target_compile_features(flynes_android_content_port_test PRIVATE cxx_std_17)
            flynes_enable_strict_warnings(flynes_android_content_port_test)
            add_test(NAME flynes_android_content_port COMMAND flynes_android_content_port_test)

            add_executable(flynes_android_content_engine_test
                tests/nearby/integration/test_android_content_engine.cpp
                tests/nearby/integration/test_android_prepared_content.cpp
                ../app/src/main/cpp/nearby/prepared_content.cpp
                tests/nearby/harness/two_engine_loopback_fixture.cpp
                ../app/src/main/cpp/nearby/session_owner.cpp
                ../app/src/main/cpp/nearby/content_port.cpp)
            target_include_directories(flynes_android_content_engine_test PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/nearby
                ${CMAKE_CURRENT_SOURCE_DIR}/src/session
                ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/nearby)
            target_link_libraries(flynes_android_content_engine_test PRIVATE flynes_session flynes_product_dual_runtime)
            target_compile_features(flynes_android_content_engine_test PRIVATE cxx_std_17)
            target_compile_definitions(flynes_android_content_engine_test PRIVATE FLYNES_SESSION_OWNER_PROJECTION_TEST=1
                FLYNES_RUNTIME_ROM_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../content/assets/roms/thwaite.nes")
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                target_compile_definitions(flynes_android_content_engine_test PRIVATE FLYNES_TEST_WRAP_ROM_LOAD=1)
                target_link_options(flynes_android_content_engine_test PRIVATE "-Wl,--wrap=nes_load_rom")
            endif()
            flynes_enable_strict_warnings(flynes_android_content_engine_test)
            add_test(NAME flynes_android_content_engine COMMAND flynes_android_content_engine_test)

            add_executable(flynes_product_quic_facts_test
                tests/nearby/contract/test_product_quic_facts.cpp)
            target_link_libraries(flynes_product_quic_facts_test PRIVATE flynes_session)
            target_include_directories(flynes_product_quic_facts_test PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/nearby
                ${CMAKE_CURRENT_SOURCE_DIR}/src/session
                ${FLYNES_QUIC_PROVIDER_ROOT}/include)
            target_compile_features(flynes_product_quic_facts_test PRIVATE cxx_std_17)
            flynes_enable_strict_warnings(flynes_product_quic_facts_test)
            add_test(NAME flynes_product_quic_facts COMMAND flynes_product_quic_facts_test)
            add_executable(flynes_product_quic_wakeup_test
                tests/nearby/integration/test_product_quic_wakeup.cpp)
            target_link_libraries(flynes_product_quic_wakeup_test PRIVATE
                flynes_session flynes_nearby_quic_provider)
            add_dependencies(flynes_product_quic_wakeup_test flynes_nearby_quic_provider_build)
            target_include_directories(flynes_product_quic_wakeup_test PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/nearby
                ${CMAKE_CURRENT_SOURCE_DIR}/src/session
                ${FLYNES_QUIC_PROVIDER_ROOT}/include)
            target_compile_features(flynes_product_quic_wakeup_test PRIVATE cxx_std_17)
            flynes_enable_strict_warnings(flynes_product_quic_wakeup_test)
            if(UNIX)
                target_link_libraries(flynes_product_quic_wakeup_test PRIVATE pthread dl m)
            endif()
            add_test(NAME flynes_product_quic_wakeup COMMAND flynes_product_quic_wakeup_test)
            set_tests_properties(flynes_product_quic_wakeup PROPERTIES TIMEOUT 30)

            add_executable(flynes_two_engine_real_quic_test
                tests/nearby/integration/test_two_engine_real_quic.cpp
                tests/nearby/harness/two_engine_loopback_fixture.cpp)
            target_link_libraries(flynes_two_engine_real_quic_test PRIVATE
                flynes_session flynes_runtime flynes_nearby_quic_provider)
            add_dependencies(flynes_two_engine_real_quic_test
                flynes_nearby_quic_provider_build)
            target_include_directories(flynes_two_engine_real_quic_test PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/src/session
                ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/nearby
                ${FLYNES_QUIC_PROVIDER_ROOT}/include)
            target_compile_features(flynes_two_engine_real_quic_test PRIVATE cxx_std_17)
            target_compile_definitions(flynes_two_engine_real_quic_test PRIVATE
                FLYNES_ENABLE_RUST_QUIC_PROVIDER=1
                FLYNES_RUNTIME_ROM_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../content/assets/roms/thwaite.nes")
            set_target_properties(flynes_two_engine_real_quic_test PROPERTIES CXX_EXTENSIONS NO)
            flynes_enable_strict_warnings(flynes_two_engine_real_quic_test)
            if(UNIX)
                target_link_libraries(flynes_two_engine_real_quic_test PRIVATE
                    pthread dl m)
            endif()
            add_test(NAME flynes_two_engine_real_quic
                     COMMAND flynes_two_engine_real_quic_test)
            set_tests_properties(flynes_two_engine_real_quic PROPERTIES
                LABELS "nearby_integration"
                TIMEOUT 300)
            add_executable(flynes_two_engine_android_quic_test
                tests/nearby/integration/test_two_engine_real_quic.cpp
                tests/nearby/harness/two_engine_loopback_fixture.cpp)
            target_link_libraries(flynes_two_engine_android_quic_test PRIVATE
                flynes_session flynes_runtime flynes_nearby_quic_provider)
            add_dependencies(flynes_two_engine_android_quic_test flynes_nearby_quic_provider_build)
            target_include_directories(flynes_two_engine_android_quic_test PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/src/session
                ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/nearby
                ${FLYNES_QUIC_PROVIDER_ROOT}/include)
            target_compile_features(flynes_two_engine_android_quic_test PRIVATE cxx_std_17)
            target_compile_definitions(flynes_two_engine_android_quic_test PRIVATE
                FLYNES_ENABLE_RUST_QUIC_PROVIDER=1 FLYNES_TEST_ANDROID_QUIC_ADAPTER=1
                FLYNES_RUNTIME_ROM_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../content/assets/roms/thwaite.nes")
            flynes_enable_strict_warnings(flynes_two_engine_android_quic_test)
            if(UNIX)
                target_link_libraries(flynes_two_engine_android_quic_test PRIVATE pthread dl m)
            endif()
            add_test(NAME flynes_two_engine_android_quic COMMAND flynes_two_engine_android_quic_test)
            set_tests_properties(flynes_two_engine_android_quic PROPERTIES
                LABELS "nearby_integration" TIMEOUT 300)
        endif()
    endif()

    # ENG-01: receive W3 pair/link tamper negatives. LoopbackTransport is the
    # in-process fixture, not Quinn; a green matrix is L1 engine fail-closed
    # evidence only. Do not copy W3's older dual_run/recovery scenario binaries:
    # W0 already has test_two_engine_dual_mvp and recovery/content sources.
    add_executable(flynes_two_engine_tamper_matrix_test
        tests/nearby/scenarios/test_two_engine_tamper_matrix.cpp
        tests/nearby/harness/two_engine_loopback_fixture.cpp)
    target_link_libraries(flynes_two_engine_tamper_matrix_test PRIVATE flynes_session)
    target_include_directories(flynes_two_engine_tamper_matrix_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/nearby
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/nearby/harness)
    target_compile_features(flynes_two_engine_tamper_matrix_test PRIVATE cxx_std_17)
    set_target_properties(flynes_two_engine_tamper_matrix_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_two_engine_tamper_matrix_test)
    add_test(NAME flynes_two_engine_tamper_matrix
             COMMAND flynes_two_engine_tamper_matrix_test)
    set_tests_properties(flynes_two_engine_tamper_matrix PROPERTIES
        LABELS "nearby_integration;nearby_protocol;nearby_auth")

    # Task 11 / CONTENT-DUAL step 1: offer/check/permission state machine.
    add_executable(flynes_content_offer_scheduler_test
        tests/nearby/integration/test_content_offer_scheduler.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/content/content_offer_scheduler.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire/sha256.cpp
    )
    target_include_directories(flynes_content_offer_scheduler_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/content
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire
    )
    target_compile_features(flynes_content_offer_scheduler_test PRIVATE cxx_std_17)
    set_target_properties(flynes_content_offer_scheduler_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_content_offer_scheduler_test)
    add_test(NAME flynes_content_offer_scheduler
             COMMAND flynes_content_offer_scheduler_test)
    set_tests_properties(flynes_content_offer_scheduler PROPERTIES
        LABELS "nearby_integration;nearby_contract")

    add_executable(flynes_content_transfer_controller_test
        tests/nearby/integration/test_content_transfer_controller.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/content/content_offer_scheduler.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/content/content_offer_wire.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/content/content_transfer_controller.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire/sha256.cpp
    )
    target_include_directories(flynes_content_transfer_controller_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/content
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire
    )
    target_compile_features(flynes_content_transfer_controller_test PRIVATE cxx_std_17)
    set_target_properties(flynes_content_transfer_controller_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_content_transfer_controller_test)
    add_test(NAME flynes_content_transfer_controller
             COMMAND flynes_content_transfer_controller_test)
    set_tests_properties(flynes_content_transfer_controller PROPERTIES
        LABELS "nearby_integration;nearby_contract")

    add_executable(flynes_link_activity_watchdog_test
        tests/nearby/recovery/test_link_activity_watchdog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/recovery/link_activity_watchdog.cpp
    )
    target_include_directories(flynes_link_activity_watchdog_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/recovery
    )
    target_compile_features(flynes_link_activity_watchdog_test PRIVATE cxx_std_17)
    set_target_properties(flynes_link_activity_watchdog_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_link_activity_watchdog_test)
    add_test(NAME flynes_link_activity_watchdog
             COMMAND flynes_link_activity_watchdog_test)
    set_tests_properties(flynes_link_activity_watchdog PROPERTIES
        LABELS "nearby_integration;nearby_contract")

    add_executable(flynes_recovery_rec_test
        tests/nearby/recovery/test_recovery_rec.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/recovery/link_activity_watchdog.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/recovery/input_sequence_ledger.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/recovery/durable_root_store.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/recovery/recovery_reducer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire/sha256.cpp
    )
    target_include_directories(flynes_recovery_rec_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/recovery
        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire
    )
    target_compile_features(flynes_recovery_rec_test PRIVATE cxx_std_17)
    set_target_properties(flynes_recovery_rec_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_recovery_rec_test)
    add_test(NAME flynes_recovery_rec
             COMMAND flynes_recovery_rec_test)
    set_tests_properties(flynes_recovery_rec PROPERTIES
        LABELS "nearby_integration;nearby_contract")
endif()
# =================== end Task 10 block ==============================

# Android (and other consumers that add core first) get the runtime library
# without pulling nestopia into catalog-only host tests.
flynes_add_runtime_library()

if(TARGET flynes_runtime)
    add_library(flynes_product_dual_runtime STATIC
        src/product/dual_runtime_port.cpp
        src/session/wire/sha256.cpp)
    target_include_directories(flynes_product_dual_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_include_directories(flynes_product_dual_runtime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/session)
    target_link_libraries(flynes_product_dual_runtime PUBLIC flynes_runtime)
    target_compile_features(flynes_product_dual_runtime PUBLIC cxx_std_17)
    set_target_properties(flynes_product_dual_runtime PROPERTIES CXX_EXTENSIONS NO POSITION_INDEPENDENT_CODE YES)
    flynes_enable_strict_warnings(flynes_product_dual_runtime)
endif()
