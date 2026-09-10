cmake_minimum_required(VERSION 3.22)

project(flynes_ios_product LANGUAGES C CXX OBJC OBJCXX)

if(NOT APPLE OR NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "ios/app is an iOS product cross-build only; host contracts live in ios/tests")
endif()
if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
    message(FATAL_ERROR "iOS product evidence requires the Xcode generator")
endif()
if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    message(FATAL_ERROR "iOS product supports exactly one architecture: arm64")
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
else()
    set(FLYNES_IOS_IS_SIMULATOR FALSE)
endif()

set(CMAKE_OSX_DEPLOYMENT_TARGET "17.0" CACHE STRING "Minimum supported iOS" FORCE)
set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO")

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
    CatalogGameDetailView.swift
    CatalogSourceManagementView.swift
    SettingsView.swift
    ControlLayoutEditorView.swift
    RunGameView.swift
    diagnostics/DiagnosticExport.swift
)
set(FLYNES_PRODUCT_OBJCXX
    bridge/FlyNesAppBridge.mm
    bridge/FlyNesRuntimeBridge.mm
    platform/FlyNesBookmarkStore.mm
    platform/CatalogScanCoordinator.mm
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

set(FLYNES_IOS_ROM_FIXTURE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../harmony/entry/src/main/resources/rawfile/from_below.nes")
set(FLYNES_IOS_ROM_LICENSE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../harmony/entry/src/main/resources/rawfile/LICENSE-from-below.txt")

add_executable(FlyNES MACOSX_BUNDLE
    ${FLYNES_PRODUCT_SWIFT}
    ${FLYNES_PRODUCT_OBJCXX}
    ${FLYNES_PRODUCT_METAL}
    bridge/FlyNes-Bridging-Header.h
    metal/FALLBACK.md
    en.lproj/Localizable.strings
    zh-Hans.lproj/Localizable.strings
    "${FLYNES_IOS_ROM_FIXTURE}"
    "${FLYNES_IOS_ROM_LICENSE}"
)
set_source_files_properties(
    en.lproj/Localizable.strings
    PROPERTIES MACOSX_PACKAGE_LOCATION "en.lproj"
)
set_source_files_properties(
    zh-Hans.lproj/Localizable.strings
    PROPERTIES MACOSX_PACKAGE_LOCATION "zh-Hans.lproj"
)
set_source_files_properties(
    "${FLYNES_IOS_ROM_FIXTURE}"
    "${FLYNES_IOS_ROM_LICENSE}"
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources
)
set_source_files_properties(${FLYNES_PRODUCT_SWIFT} PROPERTIES
    XCODE_EXPLICIT_FILE_TYPE sourcecode.swift
)
target_include_directories(FlyNES PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/bridge"
    "${CMAKE_CURRENT_SOURCE_DIR}/platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/run"
    "${CMAKE_CURRENT_SOURCE_DIR}/metal"
)
target_compile_features(FlyNES PRIVATE cxx_std_17)
target_compile_options(FlyNES PRIVATE
    $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Wall>
    $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Werror>
    $<$<COMPILE_LANGUAGE:C,CXX>:-Wpedantic>
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
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "17.0"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.flynes.app"
    XCODE_ATTRIBUTE_SUPPORTED_DEVICE_FAMILIES "1"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1"
    XCODE_ATTRIBUTE_INFOPLIST_KEY_CFBundleDisplayName "FlyNES"
)

if(FLYNES_IOS_IS_SIMULATOR)
    set_target_properties(FlyNES PROPERTIES
        XCODE_ATTRIBUTE_AD_HOC_CODE_SIGNING_ALLOWED YES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED YES
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED YES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
    )
else()
    target_link_options(FlyNES PRIVATE "-Wl,-no_adhoc_codesign")
    set_target_properties(FlyNES PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM ""
    )
endif()
