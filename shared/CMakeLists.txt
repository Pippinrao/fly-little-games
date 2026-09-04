cmake_minimum_required(VERSION 3.22)

project(flynes_shared LANGUAGES CXX)

option(FLYNES_BUILD_TESTS "Build FlyNES shared-layer host tests" OFF)

add_library(flynes_app STATIC
    src/catalog/bounded_zip_archive.cpp
    src/catalog/rom_payload_parser.cpp
    src/flynes_app.cpp
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
elseif(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    find_path(FLYNES_SHARED_ZLIB_INCLUDE zlib.h)
    find_library(FLYNES_SHARED_ZLIB_LIBRARY NAMES z)
    if(NOT FLYNES_SHARED_ZLIB_INCLUDE OR NOT FLYNES_SHARED_ZLIB_LIBRARY)
        message(FATAL_ERROR "The target SDK sysroot zlib was not found")
    endif()
    if(NOT CMAKE_SYSROOT)
        message(FATAL_ERROR "The target SDK did not define CMAKE_SYSROOT")
    endif()
    file(REAL_PATH "${CMAKE_SYSROOT}" FLYNES_SHARED_SYSROOT_REAL)
    file(REAL_PATH "${FLYNES_SHARED_ZLIB_INCLUDE}" FLYNES_SHARED_ZLIB_INCLUDE_REAL)
    file(REAL_PATH "${FLYNES_SHARED_ZLIB_LIBRARY}" FLYNES_SHARED_ZLIB_LIBRARY_REAL)
    file(RELATIVE_PATH FLYNES_SHARED_ZLIB_INCLUDE_RELATIVE
        "${FLYNES_SHARED_SYSROOT_REAL}" "${FLYNES_SHARED_ZLIB_INCLUDE_REAL}")
    file(RELATIVE_PATH FLYNES_SHARED_ZLIB_LIBRARY_RELATIVE
        "${FLYNES_SHARED_SYSROOT_REAL}" "${FLYNES_SHARED_ZLIB_LIBRARY_REAL}")
    if(FLYNES_SHARED_ZLIB_INCLUDE_RELATIVE MATCHES "^\\.\\.($|/)" OR
       IS_ABSOLUTE "${FLYNES_SHARED_ZLIB_INCLUDE_RELATIVE}" OR
       FLYNES_SHARED_ZLIB_LIBRARY_RELATIVE MATCHES "^\\.\\.($|/)" OR
       IS_ABSOLUTE "${FLYNES_SHARED_ZLIB_LIBRARY_RELATIVE}")
        message(FATAL_ERROR "The selected zlib escaped the target SDK sysroot")
    endif()
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

if(FLYNES_BUILD_TESTS)
    enable_language(C)
    enable_testing()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_executable(flynes_app_contract_test
        tests/test_app.cpp
    )
    target_link_libraries(flynes_app_contract_test PRIVATE flynes_app)
    target_compile_features(flynes_app_contract_test PRIVATE cxx_std_17)
    set_target_properties(flynes_app_contract_test PROPERTIES CXX_EXTENSIONS NO)
    flynes_enable_strict_warnings(flynes_app_contract_test)
    add_test(NAME flynes_app_contract COMMAND flynes_app_contract_test)

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
endif()
