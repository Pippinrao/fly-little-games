#!/usr/bin/env python3
"""Host checks for the unsigned iOS product IPA workflow and Sideloadly docs."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]

WORKFLOW = ".github/workflows/ios-product.yml"
STAGE1 = ".github/workflows/ios-stage1.yml"
SIDELOADLY = "ios/docs/sideloadly.md"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(relative: str) -> str:
    path = ROOT / relative
    require(path.is_file(), f"missing required file: {relative}")
    return path.read_text(encoding="utf-8")


def main() -> int:
    workflow = read(WORKFLOW)
    stage1 = read(STAGE1)
    docs = read(SIDELOADLY)

    require("ios-stage1.yml" in (ROOT / ".github/workflows").as_posix() or True,
            "Stage-1 workflow path must remain")
    require((ROOT / STAGE1).is_file(), "Stage-1 workflow must stay alongside the product job")
    require("FlyNESPortabilitySmoke" in stage1, "Stage-1 workflow must keep the smoke target")
    require("timeout-minutes: 45" in stage1, "Stage-1 timeout must not be weakened")

    require("runs-on: macos-26" in workflow, "product workflow must use macos-26")
    require("/Applications/Xcode_26.6.app/Contents/Developer" in workflow,
            "product workflow must select Xcode 26.6 by absolute path")
    require("Build version 17F113" in workflow,
            "product workflow must reject a mismatched Xcode 26.6 build")
    require("CODE_SIGNING_ALLOWED=NO" in workflow,
            "device archive must keep CODE_SIGNING_ALLOWED=NO")
    require("FlyNES-unsigned.ipa" in workflow, "workflow must emit FlyNES-unsigned.ipa")
    require("shasum -a 256" in workflow or "sha256sum" in workflow,
            "workflow must publish a SHA-256 of the unsigned IPA")
    require("CODE_SIGN_ENTITLEMENTS" in workflow or "entitlements" in workflow.lower(),
            "workflow must assert there are no extra entitlements")
    require("appex" in workflow.lower() or "app extension" in workflow.lower(),
            "workflow must assert the IPA has no app extensions")
    require("ios/app" in workflow and "-S ios/app" in workflow,
            "product workflow must configure the separate ios/app CMake target")
    require("FlyNESPortabilitySmoke" not in workflow,
            "product workflow must not replace Stage-1 smoke")
    require("APPLE_ID" not in workflow and "ASC_KEY" not in workflow
            and "p12" not in workflow.lower() and "provisioning" not in workflow.lower(),
            "CI must not introduce Apple credentials or profiles")
    require("static" in workflow.lower() or "libflynes_app.a" in workflow,
            "product must remain statically linked")
    require(
        "actions/checkout@34e114876b0b11c390a56381ad16ebd13914f8d5 # v4.3.1"
        in workflow,
        "checkout v4 must be pinned to its full official commit SHA",
    )
    require("python3 ios/tests/test_stage1_contract.py" in workflow,
            "product workflow must still run the Stage-1 host contract")
    require("python3 ios/tests/test_product_shell_contract.py" in workflow,
            "product workflow must run the product shell contract")
    require("python3 ios/tests/test_product_metal_contract.py" in workflow,
            "product workflow must run the Metal host contract")
    require("python3 ios/tests/test_product_ipa_contract.py" in workflow,
            "product workflow must run this IPA contract")

    lowered = docs.lower()
    require("sideloadly" in lowered, "Sideloadly doc must name Sideloadly")
    require("windows" in lowered, "Sideloadly refresh is a Windows flow")
    require("apple id" in lowered or "appleid" in lowered,
            "doc must say to reuse the same Apple ID")
    require("bundle id" in lowered or "com.flynes.app" in lowered,
            "doc must keep the same Bundle ID")
    require("credential" in lowered or "password" in lowered or "apple id" in lowered,
            "doc must discuss credentials")
    require("never" in lowered and ("repo" in lowered or "repository" in lowered),
            "doc must forbid putting Apple credentials in the repo")
    require("ci" in lowered and "never" in lowered,
            "doc must forbid putting Apple credentials in CI")
    require("7-day" in lowered or "7 day" in lowered,
            "doc must mention the 7-day personal team expiry")
    require("developer mode" in lowered, "doc must mention iOS Developer Mode")
    require("refresh" in lowered, "doc must describe a Sideloadly refresh")
    require(not re.search(r"AKIA[0-9A-Z]{16}|-----BEGIN PRIVATE KEY-----", docs),
            "Sideloadly doc must not contain credentials")

    cmake = read("ios/app/CMakeLists.txt")
    require("CODE_SIGNING_ALLOWED" in cmake and "NO" in cmake,
            "product CMake must disable device signing")
    require("CODE_SIGN_ENTITLEMENTS" in cmake, "product CMake must not attach entitlements")

    print("flynes_ios_product_ipa_contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"flynes_ios_product_ipa_contract: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
