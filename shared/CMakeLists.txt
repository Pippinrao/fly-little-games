cmake_minimum_required(VERSION 3.22)

project(flynes_shared LANGUAGES CXX)

option(FLYNES_BUILD_TESTS "Build FlyNES shared-layer host tests" OFF)

add_library(flynes_app STATIC
    src/catalog/bounded_zip_archive.cpp
    src/catalog/content_identity.cpp
    src/catalog/rom_payload_parser.cpp
    src/catalog/unsupported_payload_classifier.cpp
    src/app/catalog_persist.cpp
    src/app/settings_persist.cpp
    src/app/flynes_app.cpp
)
target_include_directories(flynes_app PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(flynes_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(flynes_app PRIVATE cxx_std_17)
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

add_library(flynes_session_codec STATIC
    src/session/wire/sha256.cpp
    src/session/wire/session_codec.cpp
)
target_include_directories(flynes_session_codec PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/session/wire
)
target_compile_features(flynes_session_codec PRIVATE cxx_std_17)
set_target_properties(flynes_session_codec PROPERTIES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE YES
)
flynes_enable_strict_warnings(flynes_session_codec)

add_library(flynes_session STATIC
    src/session/flynes_session.cpp
)
target_include_directories(flynes_session PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(flynes_session PRIVATE cxx_std_17)
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

if(FLYNES_BUILD_TESTS)
    enable_language(C)
    enable_testing()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    if(NOT TARGET nes_abi)
        set(NES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../core"
                         "${CMAKE_CURRENT_BINARY_DIR}/nes_core")
    endif()
    flynes_add_runtime_library()

    add_executable(flynes_runtime_test
        tests/test_runtime.cpp
    )
    target_link_libraries(flynes_runtime_test PRIVATE flynes_runtime)
    target_compile_features(flynes_runtime_test PRIVATE cxx_std_17)
    target_compile_definitions(flynes_runtime_test PRIVATE
        FLYNES_RUNTIME_ROM_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/../core/tests/fixtures/from_below.nes"
        FLYNES_RUNTIME_HEADER="${CMAKE_CURRENT_SOURCE_DIR}/include/flynes/flynes_runtime.h"
    )
    set_target_properties(flynes_runtime_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_runtime_test)
    add_test(NAME flynes_runtime COMMAND flynes_runtime_test)

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

    add_executable(flynes_app_contract_test
        tests/test_app.cpp
    )
    target_link_libraries(flynes_app_contract_test PRIVATE flynes_app)
    target_compile_features(flynes_app_contract_test PRIVATE cxx_std_17)
    set_target_properties(flynes_app_contract_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_app_contract_test)
    add_test(NAME flynes_app_contract COMMAND flynes_app_contract_test)

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

    add_executable(flynes_settings_test
        tests/test_settings.cpp
    )
    target_link_libraries(flynes_settings_test PRIVATE flynes_app)
    target_compile_features(flynes_settings_test PRIVATE cxx_std_17)
    set_target_properties(flynes_settings_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_settings_test)
    add_test(NAME flynes_settings COMMAND flynes_settings_test)

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
endif()

# Android (and other consumers that add core first) get the runtime library
# without pulling nestopia into catalog-only host tests.
flynes_add_runtime_library()
