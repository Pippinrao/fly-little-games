#!/usr/bin/env python3
"""Compliance documents must describe what actually ships.

    python content/tests/test_compliance_docs.py

The SBOM, the root NOTICE and each platform's NOTICE must agree with the
manifest, and the two licence claims that were wrong before this change must
stay retracted rather than quietly reappear.
"""

from pathlib import Path
import json
import sys


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "content" / "assets" / "builtin-games.json"

NOTICE_FILES = (
    "LICENSE",
    "app/src/main/assets/licenses/own-gplv2.txt",
    "harmony/entry/src/main/resources/rawfile/own-gplv2.txt",
    "ios/app/licenses/FlyNES-GPL-2.0.txt",
)

RETRACTED_CLAIMS = (
    ("docs/nes-arch-review/t4-roadmap.md", "MIT（GitHub mhughson）"),
    ("docs/nes-arch-review/t4-roadmap.md", "**CC0**（NESDev 论坛证实）"),
    ("docs/nes-arch-review/t4-roadmap.md", "**CC0** |"),
    ("docs/superpowers/specs/2026-08-15-nes-emulator-design.md", "From Below（MIT）"),
    ("docs/superpowers/specs/2026-08-15-nes-emulator-design.md", "Lan Master（CC0）"),
)


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

    compliance = read("docs/COMPLIANCE.md")

    # 1. Every bundled game appears in the SBOM, with its licence and revision.
    for game in games:
        asset = game["assetFilename"]
        require(asset in compliance, f"docs/COMPLIANCE.md must list {asset}")
        require(game["license"]["spdx"] in compliance,
                f"docs/COMPLIANCE.md must record the licence of {asset} "
                f"({game['license']['spdx']})")
        require(game["license"]["sourceRevision"] in compliance,
                f"docs/COMPLIANCE.md must record the pinned revision of {asset}")

    # 2. The retired single-game statements are gone.
    require("仅内置 From Below" not in compliance,
            "docs/COMPLIANCE.md must not still claim a single bundled game")
    require("无 GPLv3 组件" not in compliance,
            "docs/COMPLIANCE.md must not still claim there are no GPLv3 components")

    # 3. The copyleft reality is stated, with the aggregation reasoning and the
    #    source offer for the two GPL-3.0-or-later games.
    require("GPL-3.0-or-later" in compliance,
            "docs/COMPLIANCE.md must name the copyleft bundled games")
    require("聚合" in compliance or "aggregat" in compliance.lower(),
            "docs/COMPLIANCE.md must explain that the ROMs are aggregated, not linked")
    require("源码" in compliance,
            "docs/COMPLIANCE.md must state how the corresponding source is offered")

    # 4. The one non-reproducible upstream build is disclosed, not hidden.
    require("不可复现" in compliance or "不可字节复现" in compliance
            or "not byte-reproducible" in compliance,
            "docs/COMPLIANCE.md must disclose the non-reproducible bundled ROM")
    require("concentration_room" in compliance,
            "docs/COMPLIANCE.md must name the non-reproducible game")

    # 5. Every NOTICE names all seven games and no retired one.
    for relative in NOTICE_FILES:
        notice = read(relative)
        require("from below" not in notice.lower() and "from_below" not in notice.lower()
                and "from-below" not in notice.lower(),
                f"{relative} must not mention the retired bundled game")
        for title in ("Super Tilt Bro", "Twin Dragons", "RHDE", "Zap Ruder",
                      "Concentration Room", "Thwaite", "DABG"):
            require(title in notice, f"{relative} must credit {title}")

    # 6. The formerly wrong licence claims are retracted, not just reworded.
    for relative in {relative for relative, _ in RETRACTED_CLAIMS}:
        text = read(relative)
        require("作废" in text or "retracted" in text.lower() or "superseded" in text.lower(),
                f"{relative} must carry a retraction note for its superseded licence claims")
    for relative, claim in RETRACTED_CLAIMS:
        require(claim not in read(relative),
                f"{relative} still carries the unverified licence claim: {claim}")

    print("PASS compliance docs contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
