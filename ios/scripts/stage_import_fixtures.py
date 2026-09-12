#!/usr/bin/env python3
"""Stage licensed test fixtures for the real iOS Files picker (macOS host only).

On a dedicated, booted simulator, open Files > Browse > On My iPhone first.
The script discovers the local provider by its exact AppGroup metadata identity,
group.com.apple.FileProvider.LocalStorage, inside the explicitly selected device.

  python3 ios/scripts/stage_import_fixtures.py --udid UUID

Exactly one matching AppGroup and its existing "File Provider Storage" directory
are required. No provider directory is created. An optional --provider-root accepts
an observed absolute override for other runtimes. Both paths must resolve inside
that exact simulator's data directory and contain no symlink components. Open
Files and confirm FlyNES-Import-E2E-v1 is visible before ProductImportUITests.

--verify-fixture-only checks generation without accessing a simulator.
--cleanup removes only manifest-listed files with matching hashes, refuses extra
files, and never removes the provider root or application data. Remove test sources
through the app before cleanup. No private ROM/source directory is accepted.
"""

import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import subprocess
import sys

FOLDER = "FlyNES-Import-E2E-v1"
FIXTURE_SHA256 = "1a3ac4faf4b35640505344059ae5d91dae07cd47e1fb4d9d2a33c76391f1c555"


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fixture_files():
    repo = Path(__file__).resolve().parents[2]
    fixture = (repo / "core/tests/fixtures/from_below.nes").read_bytes()
    if sha256(fixture) != FIXTURE_SHA256 or fixture[:4] != b"NES\x1a":
        raise ValueError("repository From Below fixture changed; review before updating the expected hash")
    files = {"LICENSE-from-below.txt": (repo / "core/tests/fixtures/LICENSE-from-below.txt").read_bytes()}

    def variant(number):
        # Match CatalogSourceImportTests: change the last two payload bytes only.
        # These are catalog stress fixtures, not independent licensed games.
        result = bytearray(fixture)
        result[-2:] = number.to_bytes(2, "big")
        return bytes(result)

    files["Single/FlyNES-E2E-Single.nes"] = variant(65534)
    for number in range(100):
        payload = variant(number + 1)  # Original fixture ends in 0000; keep all 100 distinct from builtin.
        files[f"FlyNES-E2E-Hundred/FlyNES-E2E-Game-{number:03}.nes"] = payload
        files[f"FlyNES-E2E-Hundred/FlyNES-E2E-Alias-{number:03}.nes"] = payload
    hashes = {sha256(data) for name, data in files.items() if name.endswith(".nes")}
    if len(hashes) != 101 or FIXTURE_SHA256 in hashes:
        raise ValueError("fixture identities must be 100 directory games plus one distinct single import")
    manifest = {
        "owner": FOLDER,
        "fixture_sha256": FIXTURE_SHA256,
        "directory_canonical_count": 100,
        "directory_rom_count": 200,
        "files": {name: sha256(data) for name, data in files.items()},
    }
    files["manifest.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode()
    return files


def no_symlinks(path):
    if any(part.is_symlink() for part in (path, *path.parents)):
        raise ValueError(f"symlink path refused: {path}")


def discover_provider_root(data_root):
    groups = data_root / "Containers/Shared/AppGroup"
    no_symlinks(groups)
    if not groups.is_dir():
        raise ValueError("AppGroup directory is missing; open Files > Browse > On My iPhone first")
    matches = []
    for group in sorted(groups.iterdir()):
        no_symlinks(group)
        if not group.is_dir():
            continue
        metadata = group / ".com.apple.mobile_container_manager.metadata.plist"
        no_symlinks(metadata)
        if not metadata.is_file():
            continue
        with metadata.open("rb") as source:
            identity = plistlib.load(source).get("MCMMetadataIdentifier")
        if identity == "group.com.apple.FileProvider.LocalStorage":
            matches.append(group)
    if len(matches) != 1:
        raise ValueError(f"expected exactly one LocalStorage AppGroup in selected simulator, found {len(matches)}")
    provider = matches[0] / "File Provider Storage"
    no_symlinks(provider)
    if not provider.is_dir():
        raise ValueError("File Provider Storage is missing; open Files > Browse > On My iPhone first")
    return provider


def validate_existing(staging, files):
    no_symlinks(staging)
    observed = set()
    expected_directories = {parent.as_posix() for name in files for parent in Path(name).parents if parent != Path(".")}
    for path in staging.rglob("*"):
        no_symlinks(path)
        if path.is_file():
            relative = path.relative_to(staging).as_posix()
            observed.add(relative)
            if relative not in files or sha256(path.read_bytes()) != sha256(files[relative]):
                raise ValueError(f"unknown or changed file; refusing to alter {path}")
        elif path.is_dir():
            if path.relative_to(staging).as_posix() not in expected_directories:
                raise ValueError(f"unknown directory; refusing to alter {path}")
        else:
            raise ValueError(f"non-regular fixture entry: {path}")
    if observed != set(files):
        raise ValueError("incomplete fixture tree; refusing to overwrite or clean it")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--udid")
    parser.add_argument("--provider-root", type=Path, help="optional absolute local-provider root; defaults to metadata discovery")
    parser.add_argument("--verify-fixture-only", action="store_true")
    parser.add_argument("--cleanup", action="store_true")
    args = parser.parse_args()
    files = fixture_files()
    if args.verify_fixture_only:
        if args.cleanup or args.udid or args.provider_root:
            parser.error("--verify-fixture-only takes no simulator arguments")
        print("Verified repository fixture: 201 ROM files, 101 distinct payloads; 100 directory identities with two aliases each.")
        return
    if sys.platform != "darwin":
        parser.error("simulator staging is supported only on macOS")
    if not args.udid or not re.fullmatch(r"[0-9A-Fa-f]{8}(?:-[0-9A-Fa-f]{4}){3}-[0-9A-Fa-f]{12}", args.udid):
        parser.error("an explicit simulator UUID is required (never 'booted')")
    if args.provider_root is not None and not args.provider_root.is_absolute():
        parser.error("--provider-root override must be an absolute existing local-provider directory")
    devices = json.loads(subprocess.check_output(["xcrun", "simctl", "list", "devices", "--json"], text=True))
    matches = [device for group in devices["devices"].values() for device in group
               if device["udid"].lower() == args.udid.lower()]
    if len(matches) != 1 or matches[0].get("state") != "Booted":
        raise ValueError("the explicitly selected simulator must exist and be booted")
    device_data = Path.home() / "Library/Developer/CoreSimulator/Devices" / matches[0]["udid"] / "data"
    no_symlinks(device_data)
    data_root = device_data.resolve(strict=True)
    selected_provider = args.provider_root if args.provider_root is not None else discover_provider_root(data_root)
    no_symlinks(selected_provider)
    provider = selected_provider.resolve(strict=True)
    if not provider.is_dir() or data_root not in provider.parents:
        raise ValueError("provider root must be a directory strictly inside the selected simulator's data directory")
    relative = provider.relative_to(data_root).parts
    if relative[:3] == ("Containers", "Data", "Application") or relative[:2] == ("Containers", "Bundle"):
        raise ValueError("application containers are not accepted as the local Files provider root")
    staging = provider / FOLDER
    if staging.exists():
        validate_existing(staging, files)
        if args.cleanup:
            # All targets were resolved and checked before any removal. Only exact
            # generated files are unlinked; empty directories are removed bottom-up.
            for name in files:
                (staging / name).unlink()
            for path in sorted(staging.rglob("*"), key=lambda p: len(p.parts), reverse=True):
                path.rmdir()
            staging.rmdir()
            print(f"Removed only verified test fixtures: {staging}")
        else:
            print(f"Verified existing fixtures: {staging}")
    elif args.cleanup:
        print(f"No fixture directory to clean: {staging}")
    else:
        staging.mkdir()
        for name, data in files.items():
            target = staging / name
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open("xb") as output:
                output.write(data)
        validate_existing(staging, files)
        print(f"Staged and verified: {staging}")
    if not args.cleanup:
        print(f"In Files, confirm On My iPhone/{FOLDER} is visible; no app state was seeded.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error))
