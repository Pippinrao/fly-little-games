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
        "ios/scripts/run_with_timeout.py",
        "ios/scripts/validate_evidence_artifact.py",
        "ios/abi/flynes_app.symbols.txt",
        "ios/README.md",
        ".github/workflows/ios-stage1.yml",
    )
    for relative in required_files:
        require((ROOT / relative).is_file(), f"missing required Stage-1 file: {relative}")

    fixture = (ROOT / "content/assets/roms/thwaite.nes").read_bytes()
    license_text = read("content/assets/licenses/thwaite.txt")
    require(fixture[:4] == b"NES\x1a", "Stage-1 fixture is not an iNES ROM")
    require("GNU GENERAL PUBLIC LICENSE" in license_text,
            "ROM fixture must retain its licence text")
    require(
        "EE51CD9562F28195BA015D9857C6C4FC9BF67CDFB213E95F655E586B92195173"
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
    require("content/assets" in ios_cmake and "builtin-games.json" in ios_cmake,
            "the shared bundled content must be embedded as app resources")
    require("FLYNES_IOS_BUNDLED_LICENSES" in ios_cmake,
            "every bundled licence text must travel with the bundle")
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
    require("timeout-minutes: 45" in workflow,
            "workflow job must retain its 45-minute upper bound")
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
    for token in (
        "run_simctl 30 create",
        "run_simctl 30 boot",
        "run_simctl 90 bootstatus",
        "run_simctl 10 shutdown",
        "run_simctl 10 delete",
    ):
        require(token in workflow, f"simulator lifecycle is missing a bounded call: {token}")
    runner = read("ios/scripts/run_simulator_smoke.py")
    for token in ('"install"', '"get_app_container"', '"launch"', '"terminate"'):
        require(token in runner, f"simulator runner is missing simctl operation: {token}")
    require(runner.count("timeout=") >= 4,
            "install/get-container/launch/terminate calls must all be time bounded")
    require("simctl get_app_container" in runner,
            "simulator runner must inspect the launched app's data container")
    require("exit_code" in runner,
            "simulator runner must verify structured in-app completion state")
    require("FLYNES_IOS_SMOKE_PASS" in workflow,
            "simulator gate must verify the smoke success marker")
    require(
        "actions/checkout@34e114876b0b11c390a56381ad16ebd13914f8d5 # v4.3.1"
        in workflow,
        "checkout v4 must be pinned to its full official commit SHA",
    )
    require(
        "actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02 # v4.6.2"
        in workflow,
        "upload-artifact v4 must be pinned to its full official commit SHA",
    )
    require("python3 ios/tests/test_simulator_result_validator.py" in workflow,
            "workflow must execute the structured validator host tests")
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
    require("validate_evidence_artifact.py" in workflow,
            "artifact upload must pass a fail-closed inventory gate")
    require("steps.evidence_gate.outcome == 'success'" in workflow,
            "artifact upload must not run when the inventory gate fails")
    require("path: |" in workflow and "artifacts/ios-stage1/*." not in workflow,
            "artifact upload must use exact file paths, not a directory or extension glob")
    require("path: artifacts/ios-stage1\n" not in workflow,
            "artifact upload must never include the entire evidence directory")
    require("run_with_timeout.py" in workflow,
            "workflow simctl lifecycle commands must use the portable timeout wrapper")
    for token in ("minos", "17\\.0", "sdk", "26\\.5", "/usr/lib/libz\\.1\\.dylib"):
        require(token in workflow, f"final Mach-O contract is missing: {token}")

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
