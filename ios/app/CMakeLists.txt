cmake_minimum_required(VERSION 3.22)

project(flynes_ios_product LANGUAGES C CXX OBJC OBJCXX)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../../VERSION")
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../../VERSION" FLYNES_IOS_VERSION LIMIT_COUNT 1)
if(NOT FLYNES_IOS_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR "VERSION must contain a semantic version")
endif()
math(EXPR FLYNES_IOS_BUILD_NUMBER "${CMAKE_MATCH_1} * 1000000 + ${CMAKE_MATCH_2} * 1000 + ${CMAKE_MATCH_3}")

if(NOT APPLE OR NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "ios/app is an iOS product cross-build only; host contracts live in ios/tests")
endif()
if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
    message(FATAL_ERROR "iOS product evidence requires the Xcode generator")
endif()
if(NOT IS_ABSOLUTE "${CMAKE_OSX_SYSROOT}" OR NOT IS_DIRECTORY "${CMAKE_OSX_SYSROOT}")
    message(FATAL_ERROR "CMAKE_OSX_SYSROOT must be an absolute selected Apple SDK path")
endif()

enable_language(Swift)

get_filename_component(FLYNES_IOS_SDK_DIRECTORY "${CMAKE_OSX_SYSROOT}" NAME)
if(NOT FLYNES_IOS_SDK_DIRECTORY MATCHES "^iPhone(OS|Simulator)[0-9.]+\\.sdk$")
    message(FATAL_ERROR "Only the iPhoneOS and iPhoneSimulator SDKs are accepted")
endif()
if(FLYNES_IOS_SDK_DIRECTORY MATCHES "^iPhoneSimulator")
    set(FLYNES_IOS_IS_SIMULATOR TRUE)
    if(NOT CMAKE_OSX_ARCHITECTURES MATCHES "^(arm64|x86_64)$")
        message(FATAL_ERROR "Select one simulator architecture: arm64 or x86_64")
    endif()
    set(FLYNES_REQUIRED_CPU "")
else()
    set(FLYNES_IOS_IS_SIMULATOR FALSE)
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
        message(FATAL_ERROR "iPhone device builds require arm64")
    endif()
    set(FLYNES_REQUIRED_CPU "<string>arm64</string>")
endif()

set(CMAKE_OSX_DEPLOYMENT_TARGET "16.4" CACHE STRING "Minimum supported iOS")
set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO")
# Empty = unsigned device archive (Sideloadly path). Set to an Apple development
# team id to build an installable, signed arm64 bundle for a physical iPhone.
set(FLYNES_IOS_SIGNING_TEAM "" CACHE STRING "Apple development team id for device signing")

set(NES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLYNES_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../core"
                 "${CMAKE_CURRENT_BINARY_DIR}/core")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../shared"
                 "${CMAKE_CURRENT_BINARY_DIR}/shared")
flynes_add_runtime_library()

set(FLYNES_PRODUCT_SWIFT
    FlyNESApp.swift
    CatalogLibraryView.swift
    CatalogGameRow.swift
    CatalogGame.swift
    CatalogGameFactory.swift
    CatalogCoverModel.swift
    CatalogGameDetailView.swift
    CatalogSourceManagementView.swift
    SettingsView.swift
    NearbyFriendsView.swift
    NearbyPairingView.swift
    NearbyLobbyView.swift
    NearbyFriendsManageView.swift
    ControlLayoutEditorView.swift
    RunGameView.swift
    platform/ControlLayoutDraft.swift
    diagnostics/DiagnosticExport.swift
)
set(FLYNES_PRODUCT_OBJCXX
    bridge/FlyNesAppBridge.mm
    bridge/FlyNesRuntimeBridge.mm
    platform/FlyNesBookmarkStore.mm
    platform/CatalogScanCoordinator.mm
    platform/CatalogSourceService.mm
    platform/AppLocalization.mm
    platform/CatalogPresentation.mm
    platform/BuiltinGames.mm
    platform/FlyNesCoverStore.mm
    platform/RomPackage.cpp
    audio/FlyNesAudioPlayer.mm
    run/RunSurfaceViewController.mm
    run/GamepadOverlayView.mm
    metal/FlyNesMetalRenderer.mm
    metal/FlyNesDisplayLinkPacer.mm
)
set(FLYNES_PRODUCT_METAL
    metal/shaders/Nearest.metal
    metal/shaders/SharpBilinear.metal
    metal/shaders/Crt.metal
    metal/shaders/Mmpx2x.metal
    metal/shaders/ScaleFxPass0.metal
    metal/shaders/ScaleFxPass1.metal
    metal/shaders/ScaleFxPass2.metal
    metal/shaders/ScaleFxPass3.metal
    metal/shaders/ScaleFxPass4.metal
)

# The bundled homebrew comes from the one shared content directory: the manifest
# plus every ROM and licence text it declares. Nothing here names a game.
set(FLYNES_CONTENT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../content/assets")
file(GLOB FLYNES_IOS_BUNDLED_ROMS CONFIGURE_DEPENDS
    "${FLYNES_CONTENT_ROOT}/roms/*.nes")
file(GLOB FLYNES_IOS_BUNDLED_LICENSES CONFIGURE_DEPENDS
    "${FLYNES_CONTENT_ROOT}/licenses/*.txt")
set(FLYNES_IOS_BUNDLED_MANIFEST "${FLYNES_CONTENT_ROOT}/builtin-games.json")
if(NOT EXISTS "${FLYNES_IOS_BUNDLED_MANIFEST}")
    message(FATAL_ERROR "bundled content manifest is missing: ${FLYNES_IOS_BUNDLED_MANIFEST}")
endif()
if(FLYNES_IOS_BUNDLED_ROMS STREQUAL "")
    message(FATAL_ERROR "no bundled ROMs found under ${FLYNES_CONTENT_ROOT}/roms")
endif()

add_executable(FlyNES MACOSX_BUNDLE
    ${FLYNES_PRODUCT_SWIFT}
    ${FLYNES_PRODUCT_OBJCXX}
    ${FLYNES_PRODUCT_METAL}
    bridge/FlyNes-Bridging-Header.h
    metal/FALLBACK.md
    en.lproj/Localizable.strings
    zh-Hans.lproj/Localizable.strings
    "${FLYNES_IOS_BUNDLED_MANIFEST}"
    ${FLYNES_IOS_BUNDLED_ROMS}
    ${FLYNES_IOS_BUNDLED_LICENSES}
)
target_sources(FlyNES PRIVATE Assets.xcassets)
set_source_files_properties(Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
set_source_files_properties(
    "${FLYNES_IOS_BUNDLED_MANIFEST}"
    ${FLYNES_IOS_BUNDLED_ROMS}
    ${FLYNES_IOS_BUNDLED_LICENSES}
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources
)
set_source_files_properties(
    en.lproj/Localizable.strings
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources
)
set_source_files_properties(
    zh-Hans.lproj/Localizable.strings
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources
)
set_source_files_properties(
    "${FLYNES_IOS_ROM_FIXTURE}"
    "${FLYNES_IOS_ROM_LICENSE}"
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources
)
set_source_files_properties(${FLYNES_PRODUCT_SWIFT} PROPERTIES
    XCODE_EXPLICIT_FILE_TYPE sourcecode.swift
)

set(_metal_objects)
file(GLOB FLYNES_LICENSE_RESOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/licenses/*.txt")
set_source_files_properties(${FLYNES_LICENSE_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(FlyNES PRIVATE ${FLYNES_LICENSE_RESOURCES})
foreach(_shader IN LISTS FLYNES_PRODUCT_METAL)
    get_filename_component(_shader_name "${_shader}" NAME_WE)
    set(_air "${CMAKE_CURRENT_BINARY_DIR}/metal/${_shader_name}.air")
    add_custom_command(OUTPUT "${_air}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/metal"
        COMMAND xcrun --sdk "${CMAKE_OSX_SYSROOT}" metal -c
            "${CMAKE_CURRENT_SOURCE_DIR}/${_shader}" -o "${_air}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_shader}" VERBATIM)
    list(APPEND _metal_objects "${_air}")
endforeach()
set(_metallib "${CMAKE_CURRENT_BINARY_DIR}/default.metallib")
add_custom_command(OUTPUT "${_metallib}"
    COMMAND xcrun --sdk "${CMAKE_OSX_SYSROOT}" metallib ${_metal_objects} -o "${_metallib}"
    DEPENDS ${_metal_objects} VERBATIM)
set_source_files_properties("${_metallib}" PROPERTIES GENERATED YES MACOSX_PACKAGE_LOCATION Resources)
target_sources(FlyNES PRIVATE "${_metallib}")
target_include_directories(FlyNES PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/bridge"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/run"
    "${CMAKE_CURRENT_SOURCE_DIR}/metal"
    "${CMAKE_CURRENT_SOURCE_DIR}/audio"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/src"
)
target_compile_features(FlyNES PRIVATE cxx_std_17)
target_compile_options(FlyNES PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Wall>
    $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Werror>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wpedantic>
    $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Wno-gnu-conditional-omitted-operand>
    $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-fvisibility-inlines-hidden>
)
target_link_libraries(FlyNES PRIVATE
    flynes_app
    flynes_runtime
    flynes_product
    nes_abi
    "-framework Foundation"
    "-framework UIKit"
    "-framework SwiftUI"
    "-framework Metal"
    "-framework MetalKit"
    "-framework QuartzCore"
    "-framework CoreHaptics"
    "-framework CoreGraphics"
    "-framework ImageIO"
    "-framework AVFoundation"
)
target_link_options(FlyNES PRIVATE
    "-Wl,-dead_strip"
    "-Wl,-map,${CMAKE_CURRENT_BINARY_DIR}/FlyNES-LinkMap.txt"
)
set_target_properties(FlyNES PROPERTIES
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
    XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES YES
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER
        "${CMAKE_CURRENT_SOURCE_DIR}/bridge/FlyNes-Bridging-Header.h"
    XCODE_ATTRIBUTE_SWIFT_OBJC_INTERFACE_HEADER_NAME "FlyNES-Swift.h"
    XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS ""
    XCODE_ATTRIBUTE_DEAD_CODE_STRIPPING YES
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.flynes.app"
    XCODE_ATTRIBUTE_SUPPORTED_DEVICE_FAMILIES "1"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_CFBundleDisplayName "FlyNES"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
)

if(FLYNES_IOS_IS_SIMULATOR)
    set_target_properties(FlyNES PROPERTIES
        XCODE_ATTRIBUTE_AD_HOC_CODE_SIGNING_ALLOWED YES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED YES
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED YES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
    )
elseif(FLYNES_IOS_SIGNING_TEAM)
    # Physical-device qualification path. The team id is supplied by the operator
    # on the command line; no credential is stored in this repository. Automatic
    # signing lets Xcode mint a development profile for com.flynes.app, and the
    # resulting bundle is reinstalled on the same iPhone by Xcode or devicectl.
    message(STATUS "FlyNES device build signs for team ${FLYNES_IOS_SIGNING_TEAM}")
    set_target_properties(FlyNES PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED YES
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED YES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${FLYNES_IOS_SIGNING_TEAM}"
        XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER ""
    )
else()
    # Unsigned device archive: a compile/link artifact for the Sideloadly path.
    target_link_options(FlyNES PRIVATE "-Wl,-no_adhoc_codesign")
    set_target_properties(FlyNES PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
    )
endif()

option(FLYNES_IOS_BUILD_XCTESTS "Build simulator integration tests" OFF)
if(FLYNES_IOS_IS_SIMULATOR AND FLYNES_IOS_BUILD_XCTESTS)
    get_filename_component(_sdk_parent "${CMAKE_OSX_SYSROOT}" DIRECTORY)
    get_filename_component(_platform_developer "${_sdk_parent}" DIRECTORY)
    set(XCTest_LIBRARY "${_platform_developer}/Library/Frameworks/XCTest.framework" CACHE FILEPATH "" FORCE)
    set(XCTest_INCLUDE_DIR "${XCTest_LIBRARY}/Headers" CACHE PATH "" FORCE)
    find_package(XCTest REQUIRED)
    set_target_properties(FlyNES PROPERTIES ENABLE_EXPORTS YES CXX_VISIBILITY_PRESET default)
    xctest_add_bundle(FlyNESRuntimeTests FlyNES
        ../tests/PlaybackRuntimeBridgeTests.mm
        ../tests/GamepadOverlayTests.mm
        ../tests/PlaybackAudioPlayerTests.mm
        ../tests/CatalogTitleCoverTests.mm
        ../tests/CatalogSourceImportTests.mm
        ../tests/MetalPixelParityTests.mm
        ../tests/PlaybackSoakTests.mm)
    target_include_directories(FlyNESRuntimeTests PRIVATE bridge run platform "../../shared/include" "../../core/include")
    target_link_libraries(FlyNESRuntimeTests PRIVATE flynes_runtime "-framework UIKit" "-framework AVFoundation" "-framework CoreGraphics" "-framework Metal" "-framework QuartzCore")
    target_compile_features(FlyNESRuntimeTests PRIVATE cxx_std_17)
    set_target_properties(FlyNESRuntimeTests PROPERTIES
        XCODE_GENERATE_SCHEME YES
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE YES
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.flynes.runtime-tests"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED YES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@loader_path/Frameworks @executable_path/Frameworks ${_platform_developer}/Library/Frameworks")
    add_library(FlyNESUITests MODULE ../tests/ProductUITests.mm ../tests/ProductImportUITests.mm ../tests/NearbyUiParityTests.mm
        "${FLYNES_IOS_BUNDLED_MANIFEST}")
    # The UI tests assert against the shared manifest instead of hardcoding a
    # single bundled game, so the manifest travels in the test bundle too.
    set_source_files_properties("${FLYNES_IOS_BUNDLED_MANIFEST}"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    add_dependencies(FlyNESUITests FlyNES)
    target_link_libraries(FlyNESUITests PRIVATE "${XCTest_LIBRARY}" "-framework Foundation" "-framework UIKit")
    set_target_properties(FlyNESUITests PROPERTIES
        BUNDLE TRUE
        XCODE_PRODUCT_TYPE "com.apple.product-type.bundle.ui-testing"
        XCODE_GENERATE_SCHEME YES
        XCODE_ATTRIBUTE_TEST_TARGET_NAME FlyNES
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE YES
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.flynes.ui-tests"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED YES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "")
endif()
