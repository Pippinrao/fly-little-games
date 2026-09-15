#!/usr/bin/env python3
"""Contract for the bundled homebrew manifest - the single source of truth.

Runnable on Windows with a plain interpreter:

    python content/tests/test_builtin_manifest_contract.py

This file is the executable definition of "what a bundled game must prove
before it may ship". It deliberately fails while the manifest is missing so the
implementation is driven by the contract, not the other way round.
"""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
CONTENT = ROOT / "content"
MANIFEST = CONTENT / "assets" / "builtin-games.json"
LOCK = CONTENT / "sources.lock.json"
ROMS = CONTENT / "assets" / "roms"
LICENSES = CONTENT / "assets" / "licenses"

# The bundled set for this round. Adding a game means adding its id here and in
# the manifest - nothing else in any platform may need a code change.
EXPECTED_IDS = frozenset(
    {
        "builtin:super-tilt-bro",
        "builtin:twin-dragons",
        "builtin:rhde",
        "builtin:zap-ruder",
        "builtin:dabg",
        "builtin:concentration-room",
        "builtin:thwaite",
    }
)

# Licenses that carry a source-availability obligation when we redistribute the
# built binary. Those entries must state where the corresponding source lives.
COPYLEFT_SPDX = ("GPL-", "AGPL-", "LGPL-", "MPL-")

REQUIRED_GAME_FIELDS = (
    "canonicalId",
    "assetFilename",
    "titleEn",
    "titleZhHans",
    "mapper",
    "romSha256",
    "credit",
    "sortOrder",
    "license",
    "multiplayerProfile",
)
REQUIRED_LICENSE_FIELDS = ("spdx", "file", "sourceUrl", "sourceRevision")

SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
SHA1_HEX_RE = re.compile(r"^[0-9a-f]{40}$")
# An SPDX identifier, or an SPDX expression combining several of them with
# AND/OR: a bundled ROM can legitimately carry more than one license at once
# (e.g. zlib code plus CC-BY art plus a public-domain audio driver).
SPDX_TERM = r"\(*[A-Za-z0-9.+-]+\)*"
SPDX_RE = re.compile(rf"^{SPDX_TERM}(\s+(AND|OR)\s+{SPDX_TERM})*$")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> object:
    require(path.is_file(), f"missing required file: {path.relative_to(ROOT)}")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as failure:  # pragma: no cover - diagnostic path
        raise AssertionError(f"{path.relative_to(ROOT)} is not valid JSON: {failure}") from failure


def main() -> int:
    manifest = load_json(MANIFEST)
    require(isinstance(manifest, dict), "manifest root must be a JSON object")
    require(manifest.get("schemaVersion") == 1, "manifest schemaVersion must be 1")
    require(manifest.get("multiplayerProfileVersion") == 1,
            "manifest multiplayerProfileVersion must be 1")

    games = manifest.get("games")
    require(isinstance(games, list), "manifest must carry a 'games' array")
    require(len(games) == len(EXPECTED_IDS),
            f"manifest must bundle exactly {len(EXPECTED_IDS)} games, found {len(games)}")

    lock = load_json(LOCK)
    require(isinstance(lock, dict), "sources.lock.json root must be a JSON object")
    sources = lock.get("sources")
    require(isinstance(sources, list), "sources.lock.json must carry a 'sources' array")
    by_canonical = {}
    for source in sources:
        require(isinstance(source, dict), "every lock source must be an object")
        key = source.get("canonicalId")
        require(isinstance(key, str) and key, "every lock source needs a canonicalId")
        require(key not in by_canonical, f"duplicate lock record for {key}")
        by_canonical[key] = source

    seen_ids = set()
    seen_filenames = set()
    seen_sort = set()
    cover_eligible = []
    supported_multiplayer = []

    for game in games:
        require(isinstance(game, dict), "every manifest game must be an object")
        missing = [field for field in REQUIRED_GAME_FIELDS if field not in game]
        require(not missing, f"game {game.get('canonicalId')} is missing fields: {missing}")

        canonical_id = game["canonicalId"]
        location = f"game {canonical_id}"

        require(isinstance(canonical_id, str) and canonical_id.startswith("builtin:"),
                f"{location}: canonicalId must start with 'builtin:'")
        require(canonical_id not in seen_ids, f"duplicate canonicalId: {canonical_id}")
        seen_ids.add(canonical_id)

        filename = game["assetFilename"]
        require(isinstance(filename, str) and filename.endswith(".nes"),
                f"{location}: assetFilename must be a .nes filename")
        require("/" not in filename and "\\" not in filename,
                f"{location}: assetFilename must be a bare filename")
        require(filename not in seen_filenames, f"duplicate assetFilename: {filename}")
        seen_filenames.add(filename)

        for field in ("titleEn", "titleZhHans", "credit"):
            value = game[field]
            require(isinstance(value, str) and value.strip(),
                    f"{location}: {field} must be a non-empty string")

        mapper = game["mapper"]
        require(isinstance(mapper, int) and not isinstance(mapper, bool) and mapper >= 0,
                f"{location}: mapper must be a non-negative integer")

        sort_order = game["sortOrder"]
        require(isinstance(sort_order, int) and not isinstance(sort_order, bool),
                f"{location}: sortOrder must be an integer")
        require(sort_order not in seen_sort, f"{location}: duplicate sortOrder {sort_order}")
        seen_sort.add(sort_order)

        # The cover quality gate rejects flat title screens, so a device test that
        # must observe a captured cover picks its game from this flag instead of
        # hardcoding a title. Exactly one bundled game must stay capturable.
        eligible = game.get("coverEligible")
        require(isinstance(eligible, bool),
                f"{location}: coverEligible must be a boolean")
        if eligible:
            cover_eligible.append(canonical_id)

        multiplayer = game["multiplayerProfile"]
        require(isinstance(multiplayer, dict),
                f"{location}: multiplayerProfile must be an object")
        require(multiplayer.get("version") == manifest["multiplayerProfileVersion"],
                f"{location}: multiplayer profile version must match the manifest")
        eligibility = multiplayer.get("eligibility")
        require(eligibility in {"SUPPORTED", "UNSUPPORTED", "UNKNOWN"},
                f"{location}: invalid multiplayer eligibility {eligibility!r}")
        if eligibility == "SUPPORTED":
            require(multiplayer.get("maxPlayers") == 2,
                    f"{location}: supported multiplayer profile must declare maxPlayers=2")
            supported_multiplayer.append(canonical_id)

        rom_sha = game["romSha256"]
        require(isinstance(rom_sha, str) and SHA256_RE.match(rom_sha),
                f"{location}: romSha256 must be 64 lowercase hex characters")
        rom_path = ROMS / filename
        require(rom_path.is_file(), f"{location}: missing ROM asset {rom_path.relative_to(ROOT)}")
        actual = sha256_of(rom_path)
        require(actual == rom_sha,
                f"{location}: ROM hash drift - manifest {rom_sha} but file is {actual}")

        header = rom_path.read_bytes()[:4]
        require(header == b"NES\x1a", f"{location}: {filename} is not an iNES payload")

        license_block = game["license"]
        require(isinstance(license_block, dict), f"{location}: license must be an object")
        missing = [f for f in REQUIRED_LICENSE_FIELDS if f not in license_block]
        require(not missing, f"{location}: license is missing fields: {missing}")

        spdx = license_block["spdx"]
        require(isinstance(spdx, str) and SPDX_RE.match(spdx),
                f"{location}: license.spdx must be an SPDX identifier")
        license_file = license_block["file"]
        require(isinstance(license_file, str) and license_file,
                f"{location}: license.file must be a filename")
        require((LICENSES / license_file).is_file(),
                f"{location}: missing license text content/assets/licenses/{license_file}")

        source_url = license_block["sourceUrl"]
        require(isinstance(source_url, str) and source_url.startswith("https://"),
                f"{location}: license.sourceUrl must be an https URL")
        revision = license_block["sourceRevision"]
        require(isinstance(revision, str) and SHA1_HEX_RE.match(revision),
                f"{location}: license.sourceRevision must be a 40-character commit sha")

        if spdx.startswith(COPYLEFT_SPDX):
            offer = license_block.get("sourceOffer")
            require(isinstance(offer, str) and offer.strip(),
                    f"{location}: copyleft license {spdx} requires a sourceOffer statement")

        record = by_canonical.get(canonical_id)
        require(record is not None, f"{location}: no sources.lock.json record")
        require(record.get("romSha256") == rom_sha,
                f"{location}: lock romSha256 disagrees with the manifest")
        require(record.get("sourceRevision") == revision,
                f"{location}: lock sourceRevision disagrees with the manifest")
        for verbatim in ("upstreamRepo", "buildEnv", "outputRomPath"):
            value = record.get(verbatim)
            require(isinstance(value, str) and value.strip(),
                    f"{location}: lock record needs a non-empty {verbatim}")

    require(seen_ids == set(EXPECTED_IDS),
            "bundled id set mismatch: "
            f"missing={sorted(set(EXPECTED_IDS) - seen_ids)} extra={sorted(seen_ids - set(EXPECTED_IDS))}")
    require(set(by_canonical) == set(EXPECTED_IDS),
            "sources.lock.json must describe exactly the bundled games: "
            f"missing={sorted(set(EXPECTED_IDS) - set(by_canonical))} "
            f"extra={sorted(set(by_canonical) - set(EXPECTED_IDS))}")

    require(cover_eligible, "at least one bundled game must keep coverEligible true; "
                            "device cover capture has nothing it can observe otherwise")
    require(supported_multiplayer,
            "the versioned profile must expose at least one verified two-player game")

    print(f"PASS builtin manifest contract ({len(games)} games)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
