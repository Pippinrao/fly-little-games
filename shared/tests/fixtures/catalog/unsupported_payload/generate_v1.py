#!/usr/bin/env python3
"""Generate the version-1 unsupported-payload classification corpus."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import stat
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


SCHEMA_VERSION = "1"
OUTPUT_DIR = Path(__file__).resolve().parent / "v1"
HEADER = (
    "schema_version",
    "case_id",
    "blob",
    "blob_size",
    "blob_sha256",
    "expected_reason",
)
REASONS = frozenset(
    {"NESTED_ARCHIVE", "EXECUTABLE", "GAME_BOY", "SIDECAR", "UNKNOWN_FORMAT"}
)
GAME_BOY_LOGO = bytes.fromhex(
    "CEED6666CC0D000B03730083000C000D"
    "0008111F8889000EDCCC6EE6DDDDD999"
    "BBBB67636E0EECCCDDDC999FBBB9333E"
)


@dataclass(frozen=True)
class FixtureCase:
    case_id: str
    blob: bytes
    expected_reason: str


def independent_classify(payload: bytes) -> str:
    if payload[:4] in (b"PK\x03\x04", b"PK\x05\x06", b"PK\x07\x08"):
        return "NESTED_ARCHIVE"
    if payload[:2] == b"MZ":
        return "EXECUTABLE"
    logo_offset = 0x104
    if len(payload) >= logo_offset + len(GAME_BOY_LOGO):
        if payload[logo_offset : logo_offset + len(GAME_BOY_LOGO)] == GAME_BOY_LOGO:
            return "GAME_BOY"
    for value in payload[:4096]:
        if value == 0 or (value < 0x20 and value not in (0x09, 0x0A, 0x0D)):
            return "UNKNOWN_FORMAT"
    return "SIDECAR"


def game_boy_blob(extra: bytes = b"") -> bytes:
    return bytes(0x104) + GAME_BOY_LOGO + extra


def cases() -> tuple[FixtureCase, ...]:
    game_boy_minimum = game_boy_blob()
    game_boy_changed = bytearray(game_boy_minimum)
    game_boy_changed[0x104 + 17] ^= 0x01
    game_boy_offset = bytes(0x103) + GAME_BOY_LOGO + b"\x00"

    mz_game_boy = bytearray(game_boy_minimum)
    mz_game_boy[:2] = b"MZ"
    zip_game_boy = bytearray(game_boy_minimum)
    zip_game_boy[:4] = b"PK\x03\x04"

    result = (
        FixtureCase("empty", b"", "SIDECAR"),
        FixtureCase("zip_local_exact", b"PK\x03\x04", "NESTED_ARCHIVE"),
        FixtureCase("zip_local_trailing", b"PK\x03\x04trailing", "NESTED_ARCHIVE"),
        FixtureCase("zip_empty_exact", b"PK\x05\x06", "NESTED_ARCHIVE"),
        FixtureCase("zip_empty_trailing", b"PK\x05\x06trailing", "NESTED_ARCHIVE"),
        FixtureCase("zip_spanned_exact", b"PK\x07\x08", "NESTED_ARCHIVE"),
        FixtureCase("zip_spanned_trailing", b"PK\x07\x08trailing", "NESTED_ARCHIVE"),
        FixtureCase("pk_central_nonmatch", b"PK\x01\x02", "UNKNOWN_FORMAT"),
        FixtureCase("short_p", b"P", "SIDECAR"),
        FixtureCase("short_pk", b"PK", "SIDECAR"),
        FixtureCase("short_pk03", b"PK\x03", "UNKNOWN_FORMAT"),
        FixtureCase("mz_exact", b"MZ", "EXECUTABLE"),
        FixtureCase("mz_trailing", b"MZpayload", "EXECUTABLE"),
        FixtureCase("lowercase_mz", b"mz", "SIDECAR"),
        FixtureCase("mz_game_boy_priority", bytes(mz_game_boy), "EXECUTABLE"),
        FixtureCase("game_boy_minimum", game_boy_minimum, "GAME_BOY"),
        FixtureCase("game_boy_longer", game_boy_blob(b"tail"), "GAME_BOY"),
        FixtureCase("game_boy_logo_byte_diff", bytes(game_boy_changed), "UNKNOWN_FORMAT"),
        FixtureCase("game_boy_offset_minus_one", game_boy_offset, "UNKNOWN_FORMAT"),
        FixtureCase("zip_game_boy_priority", bytes(zip_game_boy), "NESTED_ARCHIVE"),
        FixtureCase("plain_ascii", b"read me.txt", "SIDECAR"),
        FixtureCase("allowed_controls", b"first\tsecond\nthird\rfourth", "SIDECAR"),
        FixtureCase("nul_prefix", b"text\x00more", "UNKNOWN_FORMAT"),
        FixtureCase("forbidden_c0_01", b"text\x01more", "UNKNOWN_FORMAT"),
        FixtureCase("forbidden_c0_0b", b"text\x0bmore", "UNKNOWN_FORMAT"),
        FixtureCase("forbidden_c0_1f", b"text\x1fmore", "UNKNOWN_FORMAT"),
        FixtureCase("del_text", b"left\x7fright", "SIDECAR"),
        FixtureCase("c1_text", b"left\x85right", "SIDECAR"),
        FixtureCase("high_bytes_text", b"\x80\xff\xfe", "SIDECAR"),
        FixtureCase("invalid_utf8_text", b"\xc0\xaf\xe2\x82", "SIDECAR"),
        FixtureCase("text_length_4095", b"A" * 4095, "SIDECAR"),
        FixtureCase("text_length_4096", b"A" * 4096, "SIDECAR"),
        FixtureCase("text_length_4097", b"A" * 4097, "SIDECAR"),
        FixtureCase("forbidden_at_4095", b"A" * 4095 + b"\x01", "UNKNOWN_FORMAT"),
        FixtureCase("forbidden_at_4096", b"A" * 4096 + b"\x01", "SIDECAR"),
        FixtureCase("nul_at_4095", b"A" * 4095 + b"\x00", "UNKNOWN_FORMAT"),
        FixtureCase("nul_at_4096", b"A" * 4096 + b"\x00", "SIDECAR"),
        FixtureCase("ordinary_binary", b"\x89PNG\r\n\x1a\n\x00binary", "UNKNOWN_FORMAT"),
    )
    for case in result:
        actual = independent_classify(case.blob)
        if actual != case.expected_reason:
            raise AssertionError(
                f"{case.case_id}: independent oracle returned {actual}, "
                f"expected {case.expected_reason}"
            )
    return result


def expected_corpus() -> dict[str, bytes]:
    contents: dict[str, bytes] = {}
    rows = ["\t".join(HEADER)]
    for case in cases():
        blob_name = case.case_id + ".bin"
        if blob_name in contents:
            raise AssertionError(f"duplicate generated filename: {blob_name}")
        contents[blob_name] = case.blob
        rows.append(
            "\t".join(
                (
                    SCHEMA_VERSION,
                    case.case_id,
                    blob_name,
                    str(len(case.blob)),
                    hashlib.sha256(case.blob).hexdigest(),
                    case.expected_reason,
                )
            )
        )
    contents["manifest.tsv"] = ("\n".join(rows) + "\n").encode("ascii")
    validate_expected_corpus(contents)
    return contents


def valid_identifier(value: str) -> bool:
    return re.fullmatch(r"[a-z][a-z0-9_]*", value) is not None


def validate_expected_corpus(contents: dict[str, bytes]) -> None:
    fixture_cases = cases()
    expected_names = {"manifest.tsv"}
    expected_names.update(case.case_id + ".bin" for case in fixture_cases)
    if set(contents) != expected_names:
        raise AssertionError("generated corpus filenames are incomplete or unexpected")
    if any("/" in name or "\\" in name or name in {".", ".."} for name in contents):
        raise AssertionError("generated corpus contains path traversal")

    manifest = contents["manifest.tsv"]
    if not manifest.endswith(b"\n") or b"\r" in manifest:
        raise AssertionError("generated manifest must use canonical LF lines")
    lines = manifest.decode("ascii").splitlines()
    if tuple(lines[0].split("\t")) != HEADER:
        raise AssertionError("generated manifest header mismatch")
    if len(lines) != len(fixture_cases) + 1:
        raise AssertionError("generated manifest case count mismatch")

    case_ids: set[str] = set()
    blob_names: set[str] = set()
    for line in lines[1:]:
        fields = line.split("\t")
        if len(fields) != len(HEADER):
            raise AssertionError("generated manifest row schema mismatch")
        version, case_id, blob_name, size, digest, reason = fields
        if version != SCHEMA_VERSION or not valid_identifier(case_id) or case_id in case_ids:
            raise AssertionError("generated case set is not canonical")
        if blob_name != case_id + ".bin" or blob_name in blob_names:
            raise AssertionError("generated blob set is not canonical")
        if size != str(len(contents[blob_name])):
            raise AssertionError("generated blob size is not canonical")
        if re.fullmatch(r"[0-9a-f]{64}", digest) is None:
            raise AssertionError("generated blob SHA-256 is not canonical")
        if digest != hashlib.sha256(contents[blob_name]).hexdigest():
            raise AssertionError("generated blob SHA-256 does not match")
        if reason not in REASONS:
            raise AssertionError("generated reason is not canonical")
        case_ids.add(case_id)
        blob_names.add(blob_name)
    if case_ids != {case.case_id for case in fixture_cases}:
        raise AssertionError("generated case IDs do not match the frozen set")


@dataclass(frozen=True)
class CorpusDiff:
    missing: tuple[str, ...]
    changed: tuple[str, ...]
    unexpected: tuple[str, ...]

    def is_empty(self) -> bool:
        return not self.missing and not self.changed and not self.unexpected


def is_filesystem_alias(path: Path) -> bool:
    try:
        status = path.lstat()
    except FileNotFoundError:
        return False
    except OSError:
        return True
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    return stat.S_ISLNK(status.st_mode) or bool(
        getattr(status, "st_file_attributes", 0) & reparse_attribute
    )


def compare_corpus(expected_files: dict[str, bytes]) -> CorpusDiff:
    if not OUTPUT_DIR.exists() and not is_filesystem_alias(OUTPUT_DIR):
        return CorpusDiff(tuple(sorted(expected_files)), (), ())
    if is_filesystem_alias(OUTPUT_DIR) or not OUTPUT_DIR.is_dir():
        return CorpusDiff((), (OUTPUT_DIR.name,), ())
    actual = {entry.name: entry for entry in OUTPUT_DIR.iterdir()}
    expected_names = set(expected_files)
    actual_names = set(actual)
    changed: list[str] = []
    for name in sorted(expected_names & actual_names):
        path = actual[name]
        if is_filesystem_alias(path) or not path.is_file():
            changed.append(name)
            continue
        try:
            if path.lstat().st_nlink != 1:
                changed.append(name)
            elif path.read_bytes() != expected_files[name]:
                changed.append(name)
        except OSError:
            changed.append(name)
    return CorpusDiff(
        tuple(sorted(expected_names - actual_names)),
        tuple(changed),
        tuple(sorted(actual_names - expected_names)),
    )


def print_diff(diff: CorpusDiff) -> None:
    for name in diff.missing:
        print(f"missing fixture corpus entry: {name}", file=sys.stderr)
    for name in diff.changed:
        print(f"changed fixture corpus entry: {name}", file=sys.stderr)
    for name in diff.unexpected:
        print(f"unexpected fixture corpus entry: {name}", file=sys.stderr)


def replace_file(path: Path, contents: bytes) -> None:
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(contents)
        os.replace(temporary, path)
    finally:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass


def generate(expected_files: dict[str, bytes]) -> int:
    if is_filesystem_alias(OUTPUT_DIR):
        print(
            f"refusing to generate: remove reparse/symlink output path {OUTPUT_DIR}",
            file=sys.stderr,
        )
        return 1
    if OUTPUT_DIR.exists() and not OUTPUT_DIR.is_dir():
        print(f"refusing to generate: remove non-directory output path {OUTPUT_DIR}", file=sys.stderr)
        return 1
    if OUTPUT_DIR.exists():
        actual = {entry.name: entry for entry in OUTPUT_DIR.iterdir()}
        blocked = sorted(
            name
            for name, path in actual.items()
            if name not in expected_files or is_filesystem_alias(path) or not path.is_file()
        )
        if blocked:
            print(
                "refusing to generate; remove these unexpected or non-file entries first: "
                + ", ".join(blocked),
                file=sys.stderr,
            )
            return 1
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, contents in expected_files.items():
        replace_file(OUTPUT_DIR / name, contents)
    diff = compare_corpus(expected_files)
    if not diff.is_empty():
        print_diff(diff)
        return 1
    print(f"generated {len(expected_files)} unsupported-payload fixture files in {OUTPUT_DIR}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="verify without modifying files")
    arguments = parser.parse_args()
    expected_files = expected_corpus()
    diff = compare_corpus(expected_files)
    if arguments.check:
        if not diff.is_empty():
            print_diff(diff)
            return 1
        print(f"unsupported-payload fixture corpus is current ({len(expected_files)} files)")
        return 0
    return generate(expected_files)


if __name__ == "__main__":
    sys.exit(main())
