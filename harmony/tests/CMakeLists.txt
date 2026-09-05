cmake_minimum_required(VERSION 3.22)

project(flynes_harmony_contract_tests LANGUAGES CXX)

include(CTest)

set(NES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLYNES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../core"
    "${CMAKE_CURRENT_BINARY_DIR}/nes_core"
)
add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../../shared"
    "${CMAKE_CURRENT_BINARY_DIR}/flynes_shared"
)
flynes_add_runtime_library()

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

add_executable(flynes_harmony_runtime_smoke_test
    runtime_smoke_test.cpp
    ../entry/src/main/cpp/runtime_smoke.cpp
)
target_include_directories(flynes_harmony_runtime_smoke_test PRIVATE
    ../entry/src/main/cpp
)
target_compile_features(flynes_harmony_runtime_smoke_test PRIVATE cxx_std_17)
set_target_properties(flynes_harmony_runtime_smoke_test PROPERTIES CXX_EXTENSIONS NO)
target_compile_definitions(flynes_harmony_runtime_smoke_test PRIVATE
    FLYNES_HARMONY_RUNTIME_ROM_FIXTURE="${CMAKE_CURRENT_LIST_DIR}/../../core/tests/fixtures/from_below.nes"
)
target_link_libraries(flynes_harmony_runtime_smoke_test PRIVATE flynes_runtime)
if(MSVC)
    target_compile_options(flynes_harmony_runtime_smoke_test PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(flynes_harmony_runtime_smoke_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()
add_test(NAME flynes_harmony_runtime_smoke COMMAND flynes_harmony_runtime_smoke_test)

add_executable(flynes_harmony_play_session_test
    play_session_test.cpp
    ../entry/src/main/cpp/play_session.cpp
)
target_include_directories(flynes_harmony_play_session_test PRIVATE
    ../entry/src/main/cpp
    "${CMAKE_CURRENT_LIST_DIR}/../../core/include"
)
target_compile_features(flynes_harmony_play_session_test PRIVATE cxx_std_17)
set_target_properties(flynes_harmony_play_session_test PROPERTIES CXX_EXTENSIONS NO)
target_compile_definitions(flynes_harmony_play_session_test PRIVATE
    FLYNES_HARMONY_RUNTIME_ROM_FIXTURE="${CMAKE_CURRENT_LIST_DIR}/../../core/tests/fixtures/from_below.nes"
)
target_link_libraries(flynes_harmony_play_session_test PRIVATE flynes_runtime)
if(MSVC)
    target_compile_options(flynes_harmony_play_session_test PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(flynes_harmony_play_session_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()
add_test(NAME flynes_harmony_play_session COMMAND flynes_harmony_play_session_test)

add_executable(flynes_harmony_product_bridge_test
    product_bridge_test.cpp
    ../entry/src/main/cpp/product_bridge.cpp
)
target_include_directories(flynes_harmony_product_bridge_test PRIVATE
    ../entry/src/main/cpp
)
target_compile_features(flynes_harmony_product_bridge_test PRIVATE cxx_std_17)
set_target_properties(flynes_harmony_product_bridge_test PROPERTIES CXX_EXTENSIONS NO)
target_link_libraries(flynes_harmony_product_bridge_test PRIVATE flynes_product)
if(MSVC)
    target_compile_options(flynes_harmony_product_bridge_test PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(flynes_harmony_product_bridge_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()
add_test(NAME flynes_harmony_product_bridge COMMAND flynes_harmony_product_bridge_test)
set_tests_properties(flynes_harmony_product_bridge PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")

add_executable(flynes_harmony_nearby_adapter_test
    nearby_adapter_test.cpp
    ../nearby/nearby_adapter.cpp
    ../nearby/friend_store.cpp
)
target_include_directories(flynes_harmony_nearby_adapter_test PRIVATE
    ../nearby
)
target_compile_features(flynes_harmony_nearby_adapter_test PRIVATE cxx_std_17)
set_target_properties(flynes_harmony_nearby_adapter_test PROPERTIES CXX_EXTENSIONS NO)
target_compile_definitions(flynes_harmony_nearby_adapter_test PRIVATE
    FLYNES_SESSION_GOLDEN_DIR="${CMAKE_CURRENT_LIST_DIR}/../../shared/schema/golden"
    FLYNES_SHARED_SESSION_HEADER="${CMAKE_CURRENT_LIST_DIR}/../../shared/include/flynes/flynes_session.h"
    FLYNES_SHARED_RUNTIME_HEADER="${CMAKE_CURRENT_LIST_DIR}/../../shared/include/flynes/flynes_runtime.h"
    FLYNES_SHARED_APP_HEADER="${CMAKE_CURRENT_LIST_DIR}/../../shared/include/flynes/flynes_app.h"
    FLYNES_ANDROID_NEARBY_DTOS="${CMAKE_CURRENT_LIST_DIR}/../../app/src/main/cpp/nearby/nearby_dtos.hpp"
    FLYNES_IOS_NEARBY_DTOS="${CMAKE_CURRENT_LIST_DIR}/../../ios/app/platform/nearby/nearby_dtos.hpp"
    FLYNES_HARMONY_NEARBY_DTOS="${CMAKE_CURRENT_LIST_DIR}/../nearby/nearby_dtos.hpp"
)
target_link_libraries(flynes_harmony_nearby_adapter_test PRIVATE flynes_session)
if(MSVC)
    target_compile_options(flynes_harmony_nearby_adapter_test PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(flynes_harmony_nearby_adapter_test PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
    )
endif()
add_test(NAME flynes_harmony_nearby_adapter COMMAND flynes_harmony_nearby_adapter_test)
