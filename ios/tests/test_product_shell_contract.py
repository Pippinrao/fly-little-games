#!/usr/bin/env python3
"""Host checks for the iOS product shell (library/settings/run/bridge)."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]

REQUIRED = (
    "ios/app/CMakeLists.txt",
    "ios/app/Info.plist.in",
    "ios/app/FlyNESApp.swift",
    "ios/app/CatalogLibraryView.swift",
    "ios/app/CatalogGameDetailView.swift",
    "ios/app/CatalogSourceManagementView.swift",
    "ios/app/SettingsView.swift",
    "ios/app/ControlLayoutEditorView.swift",
    "ios/app/RunGameView.swift",
    "ios/app/run/RunSurfaceViewController.h",
    "ios/app/run/RunSurfaceViewController.mm",
    "ios/app/run/GamepadOverlayView.h",
    "ios/app/run/GamepadOverlayView.mm",
    "ios/app/bridge/FlyNes-Bridging-Header.h",
    "ios/app/bridge/FlyNesAppBridge.h",
    "ios/app/bridge/FlyNesAppBridge.mm",
    "ios/app/bridge/FlyNesRuntimeBridge.h",
    "ios/app/bridge/FlyNesRuntimeBridge.mm",
    "ios/app/platform/FlyNesBookmarkStore.h",
    "ios/app/platform/FlyNesBookmarkStore.mm",
    "ios/app/platform/CatalogScanCoordinator.h",
    "ios/app/platform/CatalogScanCoordinator.mm",
    "ios/app/en.lproj/Localizable.strings",
    "ios/app/zh-Hans.lproj/Localizable.strings",
)

STAGE1 = (
    "ios/CMakeLists.txt",
    "ios/src/main.mm",
    "ios/src/portability_smoke.cpp",
    "ios/src/portability_smoke.hpp",
)

STAGE1_FORBIDDEN = re.compile(
    r"flynes_runtime|flynes_session|fly_runtime_|fly_session_|"
    r"NetworkExtension|MultipeerConnectivity|CoreBluetooth|MetalKit|AVAudio|"
    r"ios/app|CatalogLibraryView",
    flags=re.IGNORECASE,
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(relative: str) -> str:
    path = ROOT / relative
    require(path.is_file(), f"missing required product file: {relative}")
    return path.read_text(encoding="utf-8")


def main() -> int:
    texts = {relative: read(relative) for relative in REQUIRED}

    app = texts["ios/app/FlyNESApp.swift"]
    library = texts["ios/app/CatalogLibraryView.swift"]
    require("SettingsView" in app or "SettingsView" in library,
            "product app must present SettingsView")
    require("CatalogLibraryView" in app, "product app must still present CatalogLibraryView")

    settings = texts["ios/app/SettingsView.swift"]
    require("fly_settings_get" in settings or "settingsGet" in settings
            or "FlyNesAppBridge" in settings,
            "settings page must bind through the ObjC++ app bridge")
    require("fly_settings_apply" in settings or "settingsApply" in settings
            or "applySettings" in settings,
            "settings page must apply snapshots through the ObjC++ app bridge")

    run_swift = texts["ios/app/RunGameView.swift"]
    require("RunSurfaceViewController" in run_swift,
            "run page must host the UIKit surface controller")
    require("UIViewControllerRepresentable" in run_swift,
            "run page must embed UIKit through a representable")

    overlay = texts["ios/app/run/GamepadOverlayView.mm"]
    for token in ("DPad", "Start", "Select", "ownership", "cancel",
                  "fixed", "follow", "opacity", "haptic"):
        require(token.lower() in overlay.lower(),
                f"gamepad overlay must mention {token}")
    require("safeArea" in overlay or "safe_area" in overlay.lower()
            or "safeAreaInsets" in overlay,
            "run surface must honor landscape safe areas")

    surface = texts["ios/app/run/RunSurfaceViewController.mm"]
    require("safeAreaInsets" in surface or "safeAreaLayoutGuide" in surface,
            "run controller must apply landscape safe-area insets")
    require("touchesCancelled" in overlay or "touchesCancelled" in surface,
            "multi-touch cancel must be implemented")

    app_bridge = texts["ios/app/bridge/FlyNesAppBridge.mm"]
    require('#include <flynes/flynes_app.h>' in app_bridge
            or '#import <flynes/flynes_app.h>' in app_bridge,
            "app bridge must call flynes_app.h")
    require("fly_settings_get" in app_bridge and "fly_settings_apply" in app_bridge,
            "app bridge must call fly_settings_get/apply")
    require("fly_catalog_snapshot" in app_bridge,
            "app bridge must publish catalog snapshots")

    runtime_bridge = texts["ios/app/bridge/FlyNesRuntimeBridge.mm"]
    require('#include <flynes/flynes_runtime.h>' in runtime_bridge
            or '#import <flynes/flynes_runtime.h>' in runtime_bridge,
            "runtime bridge must call flynes_runtime.h")
    require("fly_runtime_create" in runtime_bridge, "runtime bridge must create a runtime")
    require("fly_runtime_copy_latest_frame" in runtime_bridge,
            "runtime bridge must copy RGB565 frames")

    bookmark = texts["ios/app/platform/FlyNesBookmarkStore.mm"]
    require("startAccessingSecurityScopedResource" in bookmark,
            "bookmark store must stay in platform code")
    require("bookmarkDataWithOptions" in bookmark or "NSURLBookmark" in bookmark,
            "bookmark store must create security-scoped bookmarks")

    scan = texts["ios/app/platform/CatalogScanCoordinator.mm"]
    require("borrowed_fd" in scan or "fly_scan_add_file" in scan,
            "catalog scan must feed a borrowed FD after bookmark resolve")
    require("FLYCAT01" in scan and "must never" in scan.lower(),
            "scan coordinator must forbid bookmark bytes in FLYCAT01")

    en = texts["ios/app/en.lproj/Localizable.strings"]
    zh = texts["ios/app/zh-Hans.lproj/Localizable.strings"]
    for key in ("library.title", "library.recent", "library.favorites",
                "library.all", "library.builtin", "library.search",
                "library.sources", "settings.title", "run.title"):
        require(f'"{key}"' in en, f"en strings missing {key}")
        require(f'"{key}"' in zh, f"zh-Hans strings missing {key}")

    cmake = texts["ios/app/CMakeLists.txt"]
    require("add_executable(FlyNES" in cmake, "product CMake must define FlyNES")
    require("FlyNESPortabilitySmoke" not in cmake,
            "product CMake must not build the Stage-1 smoke target")
    require("flynes_app" in cmake and "flynes_runtime" in cmake,
            "product CMake must link flynes_app and flynes_runtime")
    require("flynes_product" in cmake,
            "product CMake must link flynes_product")
    require("flynes_add_runtime_library" in cmake,
            "product must create flynes_runtime after core is present")
    require("CODE_SIGNING_ALLOWED" in cmake and "NO" in cmake,
            "device product archive must disable signing")
    require("Swift" in cmake, "product CMake must enable Swift")
    require("Metal" in cmake, "product CMake must link Metal")
    require("APPEX" not in cmake.upper() and "app_extension" not in cmake.lower(),
            "product must not add app extensions")
    require("MACOSX_BUNDLE" in cmake, "product must be a launchable app bundle")
    require("from_below.nes" in cmake,
            "product CMake must package the From Below NES fixture")
    require("LICENSE-from-below" in cmake,
            "product CMake must package the From Below LICENSE")

    plist = texts["ios/app/Info.plist.in"]
    require("CFBundleIdentifier" in plist, "product Info.plist must declare a bundle id")
    require("portability-smoke" not in plist,
            "product bundle must not reuse the Stage-1 identifier")

    stage1_cmake = read("ios/CMakeLists.txt")
    require("add_subdirectory" not in stage1_cmake or "ios/app" not in stage1_cmake,
            "Stage-1 CMake must not pull in the product target")
    require("FlyNESPortabilitySmoke" in stage1_cmake,
            "Stage-1 CMake must keep the smoke target")

    for relative in STAGE1:
        require(not STAGE1_FORBIDDEN.search(read(relative)),
                f"Stage-1 file {relative} crossed into product/runtime/media scope")

    print("flynes_ios_product_shell_contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"flynes_ios_product_shell_contract: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
