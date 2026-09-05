cmake_minimum_required(VERSION 3.22)

project(flynes_harmony_contract_tests LANGUAGES CXX)

include(CTest)

set(FLYNES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
    "${CMAKE_CURRENT_BINARY_DIR}/flynes_shared"
)

add_executable(flynes_harmony_catalog_smoke_test
    catalog_smoke_test.cpp
    ../entry/src/main/cpp/catalog_smoke.cpp
)
target_include_directories(flynes_harmony_catalog_smoke_test PRIVATE
    ../entry/src/main/cpp
)
target_compile_features(flynes_harmony_catalog_smoke_test PRIVATE cxx_std_17)
set_target_properties(flynes_harmony_catalog_smoke_test PROPERTIES CXX_EXTENSIONS NO)
target_link_libraries(flynes_harmony_catalog_smoke_test PRIVATE flynes_app)

if(MSVC)
    target_compile_options(flynes_harmony_catalog_smoke_test PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(flynes_harmony_catalog_smoke_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()

add_test(NAME flynes_harmony_catalog_smoke COMMAND flynes_harmony_catalog_smoke_test)

add_executable(flynes_harmony_session_schema_test
    schema_hash_test.cpp
)
target_include_directories(flynes_harmony_session_schema_test PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../shared/schema"
)
target_compile_features(flynes_harmony_session_schema_test PRIVATE cxx_std_17)
set_target_properties(flynes_harmony_session_schema_test PROPERTIES CXX_EXTENSIONS NO)
target_compile_definitions(flynes_harmony_session_schema_test PRIVATE
    FLYNES_HARMONY_SESSION_SCHEMA_DIR="${CMAKE_CURRENT_LIST_DIR}/../schema/flynes_session_v1"
)
if(MSVC)
    target_compile_options(flynes_harmony_session_schema_test PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(flynes_harmony_session_schema_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()
add_test(NAME flynes_harmony_session_schema COMMAND flynes_harmony_session_schema_test)
