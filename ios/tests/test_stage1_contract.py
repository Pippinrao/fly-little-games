#!/usr/bin/env python3
"""Static policy checks that are runnable on Windows before macOS CI."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(relative: str) -> str:
    path = ROOT / relative
    require(path.is_file(), f"missing required Stage-1 file: {relative}")
    return path.read_text(encoding="utf-8")


def exported_functions(header: str, prefix: str) -> set[str]:
    pattern = rf"\b(?:FLYNES_API|NES_API)\s+[^;]*?\b({prefix}_[A-Za-z0-9_]+)\s*\("
    return set(re.findall(pattern, header, flags=re.DOTALL))


def baseline(relative: str) -> set[str]:
    return {
        line.lstrip("\ufeff").strip()
        for line in read(relative).splitlines()
        if line.lstrip("\ufeff").strip()
        and not line.lstrip("\ufeff").lstrip().startswith("#")
    }


def main() -> int:
    required_files = (
        "ios/CMakeLists.txt",
        "ios/Info.plist.in",
        "ios/src/abi_layout.c",
        "ios/src/fixture_sha256.cpp",
        "ios/src/fixture_sha256.hpp",
        "ios/src/main.mm",
        "ios/src/portability_smoke.cpp",
        "ios/src/portability_smoke.hpp",
        "ios/scripts/run_simulator_smoke.py",
        "ios/abi/flynes_app.symbols.txt",
        "ios/README.md",
        ".github/workflows/ios-stage1.yml",
    )
    for relative in required_files:
        require((ROOT / relative).is_file(), f"missing required Stage-1 file: {relative}")

    fixture = (ROOT / "core/tests/fixtures/from_below.nes").read_bytes()
    license_text = read("core/tests/fixtures/LICENSE-from-below.txt")
    require(fixture[:4] == b"NES\x1a", "Stage-1 fixture is not an iNES ROM")
    require("MIT License" in license_text, "ROM fixture must retain its MIT license")
    require(
        "1A3AC4FAF4B35640505344059AE5D91DAE07CD47E1FB4D9D2A33C76391F1C555"
        in read("ios/src/portability_smoke.cpp"),
        "consumer must pin the ROM full-file SHA-256",
    )

    fly_header = read("shared/include/flynes/flynes_app.h")
    nes_header = read("core/include/nes/nes.h")
    require(
        exported_functions(fly_header, "fly") == baseline("ios/abi/flynes_app.symbols.txt"),
        "flynes_app symbol baseline does not exactly match the public header",
    )
    require(
        exported_functions(nes_header, "nes") == baseline("scripts/abi_symbols.golden.txt"),
        "nes symbol baseline does not exactly match the public header",
    )

    ios_cmake = read("ios/CMakeLists.txt")
    core_cmake = read("core/CMakeLists.txt")
    shared_cmake = read("shared/CMakeLists.txt")
    workflow = read(".github/workflows/ios-stage1.yml")
    sources = "\n".join(
        read(relative)
        for relative in (
            "ios/CMakeLists.txt",
            "ios/src/main.mm",
            "ios/src/portability_smoke.cpp",
            "ios/src/portability_smoke.hpp",
        )
    )

    require("MACOSX_BUNDLE" in ios_cmake, "consumer must be a launchable iOS app bundle")
    require("flynes_app" in ios_cmake and "nes_abi" in ios_cmake,
            "final consumer must link both shared app/catalog and emulator ABI")
    require("from_below.nes" in ios_cmake,
            "licensed ROM fixture must be embedded as an app resource")
    require("LICENSE-from-below.txt" in ios_cmake,
            "the fixture provenance notice must travel with both bundles")
    require("CMAKE_OSX_SYSROOT" in core_cmake and "FLYNES_IOS" in core_cmake,
            "core must reject zlib outside the selected Apple SDK")
    for label, cmake_text in (("core", core_cmake), ("shared", shared_cmake)):
        require("NO_DEFAULT_PATH" in cmake_text and "escaped the selected SDK" in cmake_text,
                f"{label} must contain iOS SDK-only zlib lookup and containment checks")
    require("AD_HOC_CODE_SIGNING_ALLOWED" in ios_cmake,
            "installable simulator bundle must use an explicit ad-hoc signature")
    require("-no_adhoc_codesign" in ios_cmake,
            "device link must disable the arm64 linker's automatic ad-hoc signature")
    require("../shared/src" not in ios_cmake and "catalog/content_identity" not in sources,
            "iOS consumer must not include shared private C++ implementation headers")

    require("runs-on: macos-26" in workflow, "workflow must use the macOS 26 arm64 label")
    require("submodules: recursive" in workflow, "checkout must initialize Nestopia recursively")
    require("/Applications/Xcode_26.6.app/Contents/Developer" in workflow,
            "workflow must select Xcode 26.6 by absolute path")
    require("Build version 17F113" in workflow,
            "workflow must reject a mismatched Xcode 26.6 build")
    require("build/ios-device" in workflow and "build/ios-simulator" in workflow,
            "device and simulator objects must use separate build directories")
    require('xcrun --sdk iphoneos --show-sdk-version)" = "26.5"' in workflow,
            "workflow must pin the iPhoneOS SDK version to 26.5")
    require('xcrun --sdk iphonesimulator --show-sdk-version)" = "26.5"' in workflow,
            "workflow must pin the iPhoneSimulator SDK version to 26.5")
    for token in ("iphoneos", "iphonesimulator", "arm64", "vtool", "nm", "otool"):
        require(token in workflow, f"workflow is missing evidence token: {token}")
    for token in ("simctl create", "simctl boot", "simctl install", "simctl launch --console"):
        require(token in workflow, f"simulator must actually execute smoke via: {token}")
    require("simctl get_app_container" in read("ios/scripts/run_simulator_smoke.py"),
            "simulator runner must inspect the launched app's data container")
    require("exit_code" in read("ios/scripts/run_simulator_smoke.py"),
            "simulator runner must verify structured in-app completion state")
    require("FLYNES_IOS_SMOKE_PASS" in workflow,
            "simulator gate must verify the smoke success marker")
    require("actions/upload-artifact@v4" in workflow,
            "workflow must upload inspectable Stage-1 evidence")
    require('get("isAvailable") is not True' in workflow,
            "workflow must fail unless the exact simulator runtime is available")
    require("CODE_SIGNING_ALLOWED=NO" in workflow,
            "device link proof must remain unsigned")
    require("AD_HOC_CODE_SIGNING_ALLOWED=YES" in workflow,
            "simulator smoke must explicitly request ad-hoc signing")
    require("code object is not signed at all" in workflow,
            "device evidence must reject a signed bundle and require the unsigned diagnostic")
    require("Signature=adhoc" in workflow,
            "simulator evidence must verify an ad-hoc signature")
    require("path: artifacts/ios-stage1" in workflow,
            "artifact upload must be limited to text evidence")

    forbidden = re.compile(
        r"flynes_runtime|flynes_session|fly_runtime_|fly_session_|NetworkExtension|"
        r"MultipeerConnectivity|CoreBluetooth|MetalKit|AVAudio",
        flags=re.IGNORECASE,
    )
    require(not forbidden.search(sources),
            "Stage-1 iOS consumer crossed into runtime/session/network/media product scope")

    print("flynes_ios_stage1_contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"flynes_ios_stage1_contract: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
