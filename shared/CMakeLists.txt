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
endif()
