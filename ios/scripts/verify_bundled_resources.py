#!/usr/bin/env python3
"""Verify a built FlyNES bundle ships exactly the content the shared manifest declares.

Usage: verify_bundled_resources.py <bundle-root> [evidence-output]

The manifest is the single source of truth, so this checks the bundle against it
instead of naming a game: every declared ROM and licence text must be present,
must hash to the manifest's romSha256, and must be an iNES payload. A bundle that
ships a retired or undeclared ROM fails here.
"""

from __future__ import annotations

import hashlib
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
MANIFEST = REPO_ROOT / "content" / "assets" / "builtin-games.json"
RESOURCE_DIR = "Resources"


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: verify_bundled_resources.py <bundle-root> [evidence-output]", file=sys.stderr)
        return 2
    bundle = Path(sys.argv[1])
    evidence = Path(sys.argv[2]) if len(sys.argv) > 2 else None
    if not bundle.is_dir():
        print(f"bundle is missing: {bundle}", file=sys.stderr)
        return 1

    games = json.loads(MANIFEST.read_text(encoding="utf-8"))["games"]
    if not games:
        print("manifest declares no games", file=sys.stderr)
        return 1

    resources = bundle / RESOURCE_DIR
    lines: list[str] = []
    failures: list[str] = []
    for game in games:
        name = game["assetFilename"]
        rom = resources / name
        if not rom.is_file():
            rom = next((p for p in bundle.rglob(name) if p.is_file()), rom)
        if not rom.is_file():
            failures.append(f"{game['canonicalId']}: bundled ROM {name} is missing")
            continue
        digest = sha256_of(rom)
        if digest != game["romSha256"]:
            failures.append(
                f"{game['canonicalId']}: {name} hashes to {digest}, manifest declares "
                f"{game['romSha256']}")
        if rom.read_bytes()[:4] != b"NES\x1a":
            failures.append(f"{name} is not an iNES payload")
        lines.append(f"{digest}  {name}")

        license_file = game["license"]["file"]
        license_path = resources / license_file
        if not license_path.is_file():
            license_path = next(
                (p for p in bundle.rglob(license_file) if p.is_file()), license_path)
        if not license_path.is_file():
            failures.append(f"{game['canonicalId']}: licence text {license_file} is missing")
        else:
            lines.append(f"{sha256_of(license_path)}  {license_file}")

    manifest_copy = resources / "builtin-games.json"
    if not manifest_copy.is_file():
        manifest_copy = next(
            (p for p in bundle.rglob("builtin-games.json") if p.is_file()), manifest_copy)
    if not manifest_copy.is_file():
        failures.append("the bundle does not ship builtin-games.json")
    elif manifest_copy.read_bytes() != MANIFEST.read_bytes():
        failures.append("the bundled builtin-games.json differs from content/assets")

    if evidence is not None:
        evidence.write_text("\n".join(lines) + "\n", encoding="utf-8")
    for failure in failures:
        print(f"FAIL {failure}", file=sys.stderr)
    if failures:
        return 1
    print(f"bundled resources match the manifest ({len(games)} games)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
