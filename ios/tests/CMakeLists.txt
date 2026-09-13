cmake_minimum_required(VERSION 3.22)

project(flynes_ios_stage1_host_tests LANGUAGES C CXX)

include(CTest)

set(NES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLYNES_BUILD_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../shared"
                 "${CMAKE_CURRENT_BINARY_DIR}/shared")
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../core"
                 "${CMAKE_CURRENT_BINARY_DIR}/core")

add_executable(flynes_ios_portability_smoke_host_test
    portability_smoke_test.cpp
    ../src/abi_layout.c
    ../src/fixture_sha256.cpp
    ../src/portability_smoke.cpp
)
target_include_directories(flynes_ios_portability_smoke_host_test PRIVATE
    ../src
)
target_compile_features(flynes_ios_portability_smoke_host_test PRIVATE cxx_std_17)
target_link_libraries(flynes_ios_portability_smoke_host_test PRIVATE
    flynes_app
    nes_abi
)
set_target_properties(flynes_ios_portability_smoke_host_test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    CXX_EXTENSIONS NO
)

if(MSVC)
    target_compile_options(flynes_ios_portability_smoke_host_test PRIVATE
        /W4 /WX $<$<COMPILE_LANGUAGE:CXX>:/permissive->
    )
else()
    target_compile_options(flynes_ios_portability_smoke_host_test PRIVATE
        -Wall -Wextra -Wpedantic -Werror
    )
endif()

add_test(
    NAME flynes_ios_portability_smoke_host
    COMMAND flynes_ios_portability_smoke_host_test
            "${CMAKE_CURRENT_LIST_DIR}/../../core/tests/fixtures/from_below.nes"
)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
    NAME flynes_ios_offline_title_contract
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test_offline_title_contract.py"
)
add_test(
    NAME flynes_ios_stage1_contract
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test_stage1_contract.py"
)
add_test(
    NAME flynes_ios_product_catalog_contract
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/test_product_catalog_contract.py"
)
add_test(
    NAME flynes_ios_product_shell_contract
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/test_product_shell_contract.py"
)
add_test(
    NAME flynes_ios_product_android_parity_contract
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/test_product_android_parity_contract.py"
)
add_test(
    NAME flynes_ios_product_metal_contract
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/test_product_metal_contract.py"
)
add_test(
    NAME flynes_ios_product_ipa_contract
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/test_product_ipa_contract.py"
)
add_test(
    NAME flynes_ios_simulator_result_validator
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/test_simulator_result_validator.py"
)
