#!/usr/bin/env python3
"""iOS must read the bundled games from the shared manifest.

    python ios/tests/test_builtin_games_contract.py
"""

from pathlib import Path
import json
import sys


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "content" / "assets" / "builtin-games.json"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(relative: str) -> str:
    path = ROOT / relative
    require(path.is_file(), f"missing required file: {relative}")
    return path.read_text(encoding="utf-8")


def main() -> int:
    games = json.loads(MANIFEST.read_text(encoding="utf-8"))["games"]
    require(len(games) == 7, f"the manifest must bundle 7 games, found {len(games)}")

    # 1. A manifest loader exists and reads the shared file.
    header = read("ios/app/platform/BuiltinGames.h")
    implementation = read("ios/app/platform/BuiltinGames.mm")
    require("builtin-games.json" in implementation or "builtin-games.json" in header,
            "BuiltinGames must read builtin-games.json")
    require("canonicalId" in implementation and "assetFilename" in implementation,
            "BuiltinGames must expose the manifest's canonicalId and assetFilename")

    # 2. Packaging globs the shared content instead of naming single files.
    cmake = read("ios/app/CMakeLists.txt")
    require("builtin-games.json" in cmake, "the app bundle must package builtin-games.json")
    require("content/assets" in cmake,
            "the app bundle must take the bundled games from content/assets")
    require("harmony/entry/src/main/resources/rawfile/from_below.nes" not in cmake,
            "the app bundle must not reach into another platform's rawfile")

    # 3. Playback resolves a bundled ROM through the manifest.
    run = read("ios/app/run/RunSurfaceViewController.mm")
    require("from_below" not in run and "from-below" not in run,
            "RunSurfaceViewController must not name the retired bundled ROM")
    require("BuiltinGames" in run,
            "RunSurfaceViewController must resolve a builtin launch through the manifest")

    # 4. Trusted builtin titles come from the manifest, not a hardcoded pair.
    presentation = read("ios/app/platform/CatalogPresentation.mm")
    require("From Below" not in presentation and "来自下方" not in presentation,
            "CatalogPresentation must not hardcode a bundled game's titles")

    # 5. The builtin source is prepared from every bundled game.
    source_service = read("ios/app/platform/CatalogSourceService.mm")
    require("BuiltinGames" in source_service,
            "CatalogSourceService must prepare the builtin source from the manifest")

    # 6. The single-game licence text is gone.
    require(not (ROOT / "ios/app/licenses/FromBelow-MIT.txt").exists(),
            "the retired game's licence text must be removed")

    print("PASS ios builtin-games contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
