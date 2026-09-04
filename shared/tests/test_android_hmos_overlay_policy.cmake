cmake_minimum_required(VERSION 3.22)

foreach(FLYNES_TEST_REQUIRED_VARIABLE IN ITEMS
        FLYNES_TEST_SHARED_SOURCE_DIR
        FLYNES_TEST_BINARY_DIR
        FLYNES_TEST_ANDROID_TOOLCHAIN_FILE
        FLYNES_TEST_MAKE_PROGRAM)
    if(NOT DEFINED ${FLYNES_TEST_REQUIRED_VARIABLE} OR
       "${${FLYNES_TEST_REQUIRED_VARIABLE}}" STREQUAL "")
        message(FATAL_ERROR "${FLYNES_TEST_REQUIRED_VARIABLE} is required")
    endif()
endforeach()

if(NOT EXISTS "${FLYNES_TEST_ANDROID_TOOLCHAIN_FILE}")
    message(FATAL_ERROR "Android toolchain file does not exist")
endif()
if(NOT EXISTS "${FLYNES_TEST_MAKE_PROGRAM}")
    message(FATAL_ERROR "Ninja executable does not exist")
endif()

set(FLYNES_TEST_HMOS_SDK_NATIVE "${FLYNES_TEST_BINARY_DIR}/external-hmos/native")
set(FLYNES_TEST_HMOS_INCLUDE "${FLYNES_TEST_HMOS_SDK_NATIVE}/sysroot/usr/include")
set(FLYNES_TEST_HMOS_LIBRARY
    "${FLYNES_TEST_HMOS_SDK_NATIVE}/sysroot/usr/lib/aarch64-linux-ohos/libz.a")
file(MAKE_DIRECTORY
    "${FLYNES_TEST_HMOS_INCLUDE}"
    "${FLYNES_TEST_HMOS_SDK_NATIVE}/sysroot/usr/lib/aarch64-linux-ohos")
file(WRITE "${FLYNES_TEST_HMOS_INCLUDE}/zlib.h" "/* policy-test fixture */\n")
file(WRITE "${FLYNES_TEST_HMOS_LIBRARY}" "")

set(FLYNES_TEST_ANDROID_COMMON_ARGS
    -S "${FLYNES_TEST_SHARED_SOURCE_DIR}"
    -G Ninja
    "-DCMAKE_TOOLCHAIN_FILE=${FLYNES_TEST_ANDROID_TOOLCHAIN_FILE}"
    "-DCMAKE_MAKE_PROGRAM=${FLYNES_TEST_MAKE_PROGRAM}"
    -DANDROID_ABI=arm64-v8a
    -DANDROID_PLATFORM=android-24
    -DFLYNES_BUILD_TESTS=OFF)

# A normal Android configure is the positive control. The malicious case below
# must fail because of the zlib containment policy, not an unrelated toolchain
# or compiler setup error.
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        ${FLYNES_TEST_ANDROID_COMMON_ARGS}
        -B "${FLYNES_TEST_BINARY_DIR}/android-control"
    RESULT_VARIABLE FLYNES_TEST_CONTROL_RESULT
    OUTPUT_VARIABLE FLYNES_TEST_CONTROL_OUTPUT
    ERROR_VARIABLE FLYNES_TEST_CONTROL_ERROR)
if(NOT FLYNES_TEST_CONTROL_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Android positive-control configure failed:\n"
        "${FLYNES_TEST_CONTROL_OUTPUT}\n${FLYNES_TEST_CONTROL_ERROR}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        ${FLYNES_TEST_ANDROID_COMMON_ARGS}
        -B "${FLYNES_TEST_BINARY_DIR}/android-external-hmos"
        "-DHMOS_SDK_NATIVE=${FLYNES_TEST_HMOS_SDK_NATIVE}"
        "-DFLYNES_SHARED_ZLIB_INCLUDE=${FLYNES_TEST_HMOS_INCLUDE}"
        "-DFLYNES_SHARED_ZLIB_LIBRARY=${FLYNES_TEST_HMOS_LIBRARY}"
    RESULT_VARIABLE FLYNES_TEST_REJECTION_RESULT
    OUTPUT_VARIABLE FLYNES_TEST_REJECTION_OUTPUT
    ERROR_VARIABLE FLYNES_TEST_REJECTION_ERROR)

if(FLYNES_TEST_REJECTION_RESULT EQUAL 0)
    message(FATAL_ERROR
        "Android configure accepted zlib from the external HMOS_SDK_NATIVE overlay")
endif()

set(FLYNES_TEST_REJECTION_LOG
    "${FLYNES_TEST_REJECTION_OUTPUT}\n${FLYNES_TEST_REJECTION_ERROR}")
if(NOT FLYNES_TEST_REJECTION_LOG MATCHES
        "The selected zlib escaped the target SDK sysroots")
    message(FATAL_ERROR
        "Android configure failed for the wrong reason:\n${FLYNES_TEST_REJECTION_LOG}")
endif()

message(STATUS "Android rejected the external HMOS_SDK_NATIVE zlib overlay")
