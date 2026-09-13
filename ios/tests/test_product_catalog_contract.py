#!/usr/bin/env python3
"""Host checks for the iOS product catalog shell. Runnable on Windows."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]

REQUIRED_SWIFT = (
    "ios/app/FlyNESApp.swift",
    "ios/app/CatalogLibraryView.swift",
    "ios/app/CatalogGameRow.swift",
    "ios/app/CatalogGame.swift",
)

FORBIDDEN = re.compile(
    r"import\s+(NetworkExtension|MultipeerConnectivity|CoreBluetooth|"
    r"MetalKit|AVFoundation|AVAudio)|"
    r"flynes_runtime|flynes_session|fly_runtime_|fly_session_|"
    r"source_uri|content://|SecurityScopedBookmark|UIDocumentPicker",
    flags=re.IGNORECASE,
)

STAGE1_FILES = (
    "ios/CMakeLists.txt",
    "ios/src/main.mm",
    "ios/src/portability_smoke.cpp",
    "ios/src/portability_smoke.hpp",
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(relative: str) -> str:
    path = ROOT / relative
    require(path.is_file(), f"missing required product file: {relative}")
    return path.read_text(encoding="utf-8")


def main() -> int:
    sources = {relative: read(relative) for relative in REQUIRED_SWIFT}
    combined = "\n".join(sources.values())

    require("import SwiftUI" in sources["ios/app/FlyNESApp.swift"],
            "product app must be a SwiftUI App")
    require("@main" in sources["ios/app/FlyNESApp.swift"],
            "product app must declare @main")
    require("CatalogLibraryView" in sources["ios/app/FlyNESApp.swift"],
            "product app must present CatalogLibraryView")

    library = sources["ios/app/CatalogLibraryView.swift"]
    require("struct CatalogLibraryView" in library, "library view is missing")
    require("List" in library or "ForEach" in library,
            "library view must list catalog games")
    require("catalog snapshot" in library.lower() or "CatalogSnapshot" in library,
            "library view must be driven by a catalog snapshot")
    lowered = library.lower()
    for token in ("recent", "favorite", "builtin", "search"):
        require(token in lowered, f"library view must expose a {token} placeholder")
    require("CatalogGameDetailView" in library, "library view must open a detail placeholder")
    require("CatalogSourceManagementView" in library,
            "library view must expose a source-management placeholder")

    game = sources["ios/app/CatalogGame.swift"]
    require("canonicalId" in game, "game model must carry canonicalId")
    require("displayName" in game, "game model must carry displayName")
    require(not re.search(r"\b(let|var)\s+\w*(uri|bookmark)\w*", game, flags=re.IGNORECASE),
            "game model must not store locators")

    row = sources["ios/app/CatalogGameRow.swift"]
    require("struct CatalogGameRow" in row, "game row view is missing")
    # Android cards render the locale title and the other language as metadata, with
    # the filename kept only as the untranslated fallback.
    require("titlePrimary" in row and "titleSecondary" in row,
            "game row must show the locale title and its secondary language")

    # Automatic covers: the store writes SHA-256-named 320x240 PNGs into
    # covers/v1, and capture samples the runtime's authoritative frame sequence.
    store = read("ios/app/platform/FlyNesCoverStore.mm")
    require("covers/v1" in store, "covers must live in the private covers/v1 directory")
    require("CC_SHA256" in store, "cover filenames must be SHA-256 of the canonical id")
    require("kCoverWidth = 320" in store and "kCoverHeight = 240" in store,
            "covers must be persisted at 320x240")
    require("std::rename(" in store, "cover writes must be atomic")
    require("NSURLIsExcludedFromBackupKey" in store,
            "captured covers must stay out of device backups")
    require(not re.search(r"\b(NSLog|print)\b.*canonicalId", store),
            "cover storage must not log canonical ids")

    policy = read("ios/app/platform/CoverCapturePolicy.hpp")
    require("GameCoverPolicy.hpp" in policy and "note_frame" in policy
            and "consider" in policy,
            "capture policy must apply the shared sampling offsets and quality gate")
    run = read("ios/app/run/RunSurfaceViewController.mm")
    require("CoverCaptureSession" in run and "captureCoverFrame" in run,
            "the run surface must own one capture session per play session")
    require("copyLatestRgb565FrameWithSequence" in run,
            "capture must sample the runtime frame sequence, not a view counter")
    require("game_cover_score" in run,
            "capture must score with the shared Android FrameQuality port")
    require("QOS_CLASS_UTILITY" in run,
            "scoring and persistence must not block the display link")

    # The presentation helper must be compiled into the product target.
    app_cmake = read("ios/app/CMakeLists.txt")
    for source in ("platform/CatalogPresentation.mm", "platform/FlyNesCoverStore.mm"):
        require(source in app_cmake, f"{source} must be compiled into the product")

    # Device qualification entry points.
    require("FLYNES_IOS_SIGNING_TEAM" in app_cmake,
            "device signing must be an explicit opt-in cache variable")
    for script in ("ios/scripts/build_device.sh", "ios/scripts/package_device.sh",
                   "ios/docs/iphone-device-acceptance.md"):
        require((ROOT / script).is_file(), f"missing device entry point: {script}")

    require(not FORBIDDEN.search(combined),
            "product Swift sources imported Stage-1-forbidden networking/runtime APIs")

    for relative in STAGE1_FILES:
        text = read(relative)
        require("ios/app" not in text and "CatalogLibraryView" not in text,
                f"Stage-1 file {relative} must not include the product catalog shell")

    print("flynes_ios_product_catalog_contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"flynes_ios_product_catalog_contract: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
