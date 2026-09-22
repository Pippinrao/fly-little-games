cmake_minimum_required(VERSION 3.22)
project(nearby_crypto_test LANGUAGES CXX)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
set(FLYNES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLYNES_ENABLE_RUST_QUIC_PROVIDER ON CACHE BOOL "" FORCE)
add_subdirectory("${REPO}/core" core)
add_subdirectory("${REPO}/shared" shared)
add_library(nearby_crypto_test SHARED
    crypto_port_test_jni.cpp
    mvp_peer_test_jni.cpp
    "${REPO}/app/src/main/cpp/nearby/crypto_port.cpp"
    "${REPO}/app/src/main/cpp/nearby/crypto_jni.cpp")
target_link_libraries(nearby_crypto_test PRIVATE flynes_session_ports flynes_lan_mvp)
target_include_directories(nearby_crypto_test PRIVATE
    "${REPO}/app/src/main/cpp/nearby" "${REPO}/shared/include"
    "${REPO}/shared/src/session")
target_compile_features(nearby_crypto_test PRIVATE cxx_std_17)
target_compile_definitions(nearby_crypto_test PRIVATE FLYNES_CRYPTO_JNI_TEST=1)
target_compile_options(nearby_crypto_test PRIVATE -Wall -Wextra -Werror -fvisibility=hidden)
# The test-only inbox cannot interpose on an independently loaded product library.
target_link_options(nearby_crypto_test PRIVATE -Wl,-Bsymbolic)
