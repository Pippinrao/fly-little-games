#!/usr/bin/env python3
"""HarmonyOS must read the bundled games from the shared manifest.

    python harmony/tests/test_builtin_games_contract.py

Deliberately separate from test_harmony_product_contract.py, which carries an
unrelated pre-existing failure, so this contract can be read on its own.
"""

from pathlib import Path
import hashlib
import json
import sys


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "content" / "assets" / "builtin-games.json"
RAWFILE = ROOT / "harmony" / "entry" / "src" / "main" / "resources" / "rawfile"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(relative: str) -> str:
    path = ROOT / relative
    require(path.is_file(), f"missing required file: {relative}")
    return path.read_text(encoding="utf-8")


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    games = manifest["games"]
    require(len(games) == 7, f"the manifest must bundle 7 games, found {len(games)}")

    # 1. A manifest loader exists and reads the shared file.
    loader = read("harmony/entry/src/main/ets/service/BuiltinGames.ets")
    require("builtin-games.json" in loader,
            "BuiltinGames.ets must read builtin-games.json")
    require("assetFilename" in loader and "canonicalId" in loader,
            "BuiltinGames.ets must expose the manifest's canonicalId and assetFilename")

    # 2. Nothing opens the retired ROM by name any more.
    play = read("harmony/entry/src/main/ets/service/PlayService.ets")
    require("from_below" not in play and "from-below" not in play,
            "PlayService must not name the retired bundled ROM")
    require("BuiltinGames" in play or "builtinAsset" in play,
            "PlayService must resolve a builtin launch through the manifest")

    # 3. The catalog projection is manifest driven.
    catalog = read("harmony/entry/src/main/ets/service/CatalogProductService.ets")
    require("from_below" not in catalog and "From Below" not in catalog,
            "CatalogProductService must not hardcode a bundled game")
    require("BuiltinGames" in catalog,
            "CatalogProductService must project the bundled games from the manifest")

    # 4. Licence rows come from the manifest too.
    licences = read("harmony/entry/src/main/ets/pages/LicenseModel.ets")
    require("from-below" not in licences and "From Below" not in licences,
            "LicenseModel must not hardcode the retired bundled game")

    # 5. Localized strings must not name a single bundled game.
    for locale in ("base", "zh_CN"):
        strings = read(f"harmony/entry/src/main/resources/{locale}/element/string.json")
        require("From Below" not in strings and "来自下方" not in strings,
                f"{locale} strings must not name a single bundled game")

    # 6. Any ROM staged into the rawfile must be an exact copy of content/assets:
    #    the shared content is the single source of truth for every platform.
    manifest_hashes = {game["assetFilename"]: game["romSha256"] for game in games}
    for path in sorted(RAWFILE.glob("*.nes")):
        require(path.name in manifest_hashes,
                f"a staged ROM is not declared by the manifest: {path.name}")
        actual = hashlib.sha256(path.read_bytes()).hexdigest()
        require(actual == manifest_hashes[path.name],
                f"the staged ROM drifted from content/assets: {path.name}")
    require(not (RAWFILE / "LICENSE-from-below.txt").exists(),
            "harmony rawfile must not keep the retired licence text")
    require((ROOT / "tools" / "content" / "sync-builtin-content.ps1").is_file(),
            "the rawfile staging script must exist")

    print("PASS harmony builtin-games contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
