cmake_minimum_required(VERSION 3.22)

project(flynes_ios_stage1 LANGUAGES C CXX OBJCXX)

if(NOT APPLE OR NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "ios/ is an iOS cross-build only; use ios/tests for host smoke")
endif()
if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
    message(FATAL_ERROR "Stage-1 iOS evidence requires the Xcode generator")
endif()
if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "Stage-1 supports exactly one architecture: arm64")
endif()
if(NOT IS_ABSOLUTE "${CMAKE_OSX_SYSROOT}" OR NOT IS_DIRECTORY "${CMAKE_OSX_SYSROOT}")
    message(FATAL_ERROR "CMAKE_OSX_SYSROOT must be an absolute selected Apple SDK path")
endif()

get_filename_component(FLYNES_IOS_SDK_DIRECTORY "${CMAKE_OSX_SYSROOT}" NAME)
if(NOT FLYNES_IOS_SDK_DIRECTORY MATCHES "^iPhone(OS|Simulator)[0-9.]+\\.sdk$")
    message(FATAL_ERROR "Only the iPhoneOS and iPhoneSimulator SDKs are accepted")
endif()
if(FLYNES_IOS_SDK_DIRECTORY MATCHES "^iPhoneSimulator")
    set(FLYNES_IOS_IS_SIMULATOR TRUE)
else()
    set(FLYNES_IOS_IS_SIMULATOR FALSE)
endif()

set(CMAKE_OSX_DEPLOYMENT_TARGET "17.0" CACHE STRING "Minimum supported iOS" FORCE)
set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO")

set(NES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLYNES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../shared"
                 "${CMAKE_CURRENT_BINARY_DIR}/shared")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../core"
                 "${CMAKE_CURRENT_BINARY_DIR}/core")

# The portability smoke packages the same shared content as the product: the
# manifest plus every ROM and licence it declares, with no per-game CMake entry.
set(FLYNES_CONTENT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../content/assets")
file(GLOB FLYNES_IOS_BUNDLED_ROMS CONFIGURE_DEPENDS "${FLYNES_CONTENT_ROOT}/roms/*.nes")
file(GLOB FLYNES_IOS_BUNDLED_LICENSES CONFIGURE_DEPENDS "${FLYNES_CONTENT_ROOT}/licenses/*.txt")
set(FLYNES_IOS_BUNDLED_MANIFEST "${FLYNES_CONTENT_ROOT}/builtin-games.json")

add_executable(FlyNESPortabilitySmoke MACOSX_BUNDLE
    src/abi_layout.c
    src/fixture_sha256.cpp
    src/fixture_sha256.hpp
    src/main.mm
    src/portability_smoke.cpp
    src/portability_smoke.hpp
    app/platform/BuiltinGames.mm
    "${FLYNES_IOS_BUNDLED_MANIFEST}"
    ${FLYNES_IOS_BUNDLED_ROMS}
    ${FLYNES_IOS_BUNDLED_LICENSES}
)
set_source_files_properties(
    "${FLYNES_IOS_BUNDLED_MANIFEST}"
    ${FLYNES_IOS_BUNDLED_ROMS}
    ${FLYNES_IOS_BUNDLED_LICENSES}
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources
)
target_include_directories(FlyNESPortabilitySmoke PRIVATE
    src
    app/platform
)
target_compile_features(FlyNESPortabilitySmoke PRIVATE cxx_std_17)
target_compile_options(FlyNESPortabilitySmoke PRIVATE
    -Wall
    -Wextra
    -Werror
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wpedantic>
    $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-fvisibility-inlines-hidden>
)
target_link_libraries(FlyNESPortabilitySmoke PRIVATE
    flynes_app
    nes_abi
    "-framework Foundation"
    "-framework UIKit"
)
target_link_options(FlyNESPortabilitySmoke PRIVATE
    "-Wl,-dead_strip"
    "-Wl,-map,${CMAKE_CURRENT_BINARY_DIR}/FlyNESPortabilitySmoke-LinkMap.txt"
)
set_target_properties(FlyNESPortabilitySmoke PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    CXX_EXTENSIONS NO
    CXX_VISIBILITY_PRESET hidden
    OBJCXX_STANDARD 17
    OBJCXX_STANDARD_REQUIRED YES
    OBJCXX_EXTENSIONS NO
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    XCODE_ATTRIBUTE_DEAD_CODE_STRIPPING YES
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "17.0"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.flynes.portability-smoke"
    XCODE_ATTRIBUTE_SUPPORTED_DEVICE_FAMILIES "1"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1"
)

if(FLYNES_IOS_IS_SIMULATOR)
    # CoreSimulator installs an ad-hoc signed test bundle. This uses no Apple
    # account, team, provisioning profile, entitlement, or distribution key.
    set_target_properties(FlyNESPortabilitySmoke PROPERTIES
        XCODE_ATTRIBUTE_AD_HOC_CODE_SIGNING_ALLOWED YES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED YES
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED YES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
    )
else()
    # arm64 ld can emit a linker ad-hoc signature even when Xcode's CodeSign
    # phase is disabled. Suppress it so the device artifact is truly unsigned.
    target_link_options(FlyNESPortabilitySmoke PRIVATE "-Wl,-no_adhoc_codesign")
    set_target_properties(FlyNESPortabilitySmoke PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
    )
endif()
