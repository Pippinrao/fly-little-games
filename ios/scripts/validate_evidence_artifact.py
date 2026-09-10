#!/usr/bin/env python3
"""Fail-closed inventory gate for the text-only iOS Stage-1 artifact."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import stat
import sys
from typing import NamedTuple


MAX_FILE_BYTES = 1024 * 1024
MAX_TOTAL_BYTES = 8 * 1024 * 1024

REQUIRED_SUCCESS_FILES = frozenset(
    {
        "SHA256SUMS.txt",
        "cmake-sdk-zlib-selection.txt",
        "cmake-version.txt",
        "device-architectures.txt",
        "device-codesign.txt",
        "device-file.txt",
        "device-final-c-symbols.txt",
        "device-fly-symbols.txt",
        "device-link-flynes-app.txt",
        "device-link-nes-abi.txt",
        "device-link-nestopia.txt",
        "device-nes-symbols.txt",
        "device-otool.txt",
        "device-resource-hashes.txt",
        "device-vtool.txt",
        "evidence-inventory.txt",
        "expected-nes-symbols.txt",
        "host-contract.txt",
        "host-validator.txt",
        "iphoneos-zlib.txt",
        "iphonesimulator-zlib.txt",
        "sdk-paths.txt",
        "simulator-architectures.txt",
        "simulator-codesign.txt",
        "simulator-file.txt",
        "simulator-final-c-symbols.txt",
        "simulator-fly-symbols.txt",
        "simulator-launch.log",
        "simulator-link-flynes-app.txt",
        "simulator-link-nes-abi.txt",
        "simulator-link-nestopia.txt",
        "simulator-nes-symbols.txt",
        "simulator-otool.txt",
        "simulator-resource-hashes.txt",
        "simulator-result.json",
        "simulator-runtimes.json",
        "simulator-vtool.txt",
        "xcode-version.txt",
    }
)
ALLOWED_FILES = REQUIRED_SUCCESS_FILES


class EvidenceEntry(NamedTuple):
    name: str
    size: int


def validate_evidence_directory(
    directory: Path, *, require_complete: bool
) -> list[EvidenceEntry]:
    if directory.is_symlink() or not directory.is_dir():
        raise RuntimeError("evidence path is not a real directory")
    entries: list[EvidenceEntry] = []
    total_size = 0
    for path in sorted(directory.iterdir(), key=lambda item: item.name):
        if path.name not in ALLOWED_FILES:
            raise RuntimeError(f"unexpected evidence artifact entry: {path.name}")
        metadata = path.lstat()
        if path.is_symlink() or not stat.S_ISREG(metadata.st_mode):
            raise RuntimeError(f"evidence entry is not a regular file: {path.name}")
        if metadata.st_nlink != 1:
            raise RuntimeError(f"evidence entry must not be hard-linked: {path.name}")
        if metadata.st_size <= 0 or metadata.st_size > MAX_FILE_BYTES:
            raise RuntimeError(f"evidence file is outside the size bound: {path.name}")
        with path.open("rb") as stream:
            payload = stream.read(MAX_FILE_BYTES + 1)
        if len(payload) != metadata.st_size or len(payload) > MAX_FILE_BYTES:
            raise RuntimeError(f"evidence file changed while being read: {path.name}")
        if b"\x00" in payload:
            raise RuntimeError(f"evidence file contains binary NUL bytes: {path.name}")
        try:
            text = payload.decode("utf-8", errors="strict")
            if path.suffix == ".json":
                json.loads(text)
        except (UnicodeError, json.JSONDecodeError) as error:
            raise RuntimeError(f"evidence file is not valid text: {path.name}") from error
        total_size += len(payload)
        if total_size > MAX_TOTAL_BYTES:
            raise RuntimeError("evidence artifact exceeds its total size bound")
        entries.append(EvidenceEntry(path.name, len(payload)))

    names = {entry.name for entry in entries}
    if require_complete and names != REQUIRED_SUCCESS_FILES:
        missing = sorted(REQUIRED_SUCCESS_FILES - names)
        raise RuntimeError(f"successful Stage-1 evidence is incomplete: {missing!r}")
    return entries


def write_inventory(entries: list[EvidenceEntry], destination: Path) -> None:
    lines = ["size_bytes file\n"]
    lines.extend(f"{entry.size:10d} {entry.name}\n" for entry in entries)
    temporary = destination.with_name(destination.name + ".tmp")
    temporary.write_text("".join(lines), encoding="utf-8")
    temporary.replace(destination)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", required=True, type=Path)
    parser.add_argument("--require-complete", action="store_true")
    parser.add_argument("--write-inventory", type=Path)
    args = parser.parse_args()
    try:
        entries = validate_evidence_directory(
            args.directory, require_complete=args.require_complete
        )
        if args.write_inventory is not None:
            expected = args.directory / "evidence-inventory.txt"
            if args.write_inventory.resolve(strict=False) != expected.resolve(strict=False):
                raise RuntimeError("inventory output must be evidence-inventory.txt")
            write_inventory(entries, args.write_inventory)
    except (OSError, RuntimeError) as error:
        print(f"validate_evidence_artifact: FAIL: {error}", file=sys.stderr)
        return 1
    for entry in entries:
        print(f"{entry.size:10d} {entry.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
