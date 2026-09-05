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

    game = sources["ios/app/CatalogGame.swift"]
    require("canonicalId" in game, "game model must carry canonicalId")
    require("displayName" in game, "game model must carry displayName")
    require(not re.search(r"\b(let|var)\s+\w*(uri|bookmark)\w*", game, flags=re.IGNORECASE),
            "game model must not store locators")

    row = sources["ios/app/CatalogGameRow.swift"]
    require("struct CatalogGameRow" in row, "game row view is missing")
    require("displayName" in row, "game row must show displayName")

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
