#!/usr/bin/env python3
"""Generate the version-1 cross-language bounded ZIP-payload fixture corpus."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import os
import re
import stat
import struct
import sys
import tempfile
import zlib
from dataclasses import dataclass, replace
from pathlib import Path


SCHEMA_VERSION = "1"
OUTPUT_DIR = Path(__file__).resolve().parent / "v1"
LOCAL_SIGNATURE = 0x04034B50
CENTRAL_SIGNATURE = 0x02014B50
END_SIGNATURE = 0x06054B50
DESCRIPTOR_SIGNATURE = 0x08074B50

# These raw DEFLATE streams preserve the frozen v1 ZIP bytes. zlib and
# zlib-ng can encode the same payload differently, even at the same level.
# The two complete streams were extracted from their v1 fixtures; the
# truncated stream restores the final 00 byte before applying the original
# truncation below. Keep these independent of the output files at runtime.
LARGE_RAW_DEFLATE = bytes.fromhex(
    "138e59facebce1a450f492b766f52704a316bf31ad3b2e10b9e8b549ed31fe88"
    "85af8c6b8ef2852f7869547d84376cfe0bc3aac33ca1f39e1b541ee20e99fb4c"
    "bfe22057f09ca77ae5073883663fd12ddbcf1138ebb14ee93ef680998fb44bf6"
    "b2f9cf78a855bc87d56ffa03cda2dd2cbed3ee6b14ee62f6997a4fbd602793f7"
    "94bb6af93b18bd26df51cddbcee039e9b64aeeb6ff1e136f29e76cfde73ee1a6"
    "52f696bf6efd3714b336ff71edbbae90b9e9b74bef35f98c8dbf9c7baecaa56f"
    "f8e9d47d45366dfd0fc7aecb32a9ebbe3b745e924e59fbcdbee3a254f29aaf76"
    "ed172493567fb16d3b2f91b8eab34deb39f184959fac5bce8ac5aff868d57c46"
    "346ef907cba6d322b1cbde5b349e121ef5ffa8ff47fd3feaff51ff8ffa7fd4ff"
    "a3fe1ff5ffa8ff47fd3feaff51ff8ffa7fd4ffa3fe1ff5ffa8ff47fd3feaff51"
    "ff8ffa7fd4ffa3fe1ff5ffa8ff47fd3feaff51ff8ffa7fd4ffa3fe1ff5bfde88"
    "f03f00"
)
DIRECTORY_RAW_DEFLATE = bytes.fromhex(
    "05c1510a00200844c1abecd5c25e204981fad3ed9b999e58df7ce2743aa591a808ac"
    "990adf6879501f"
)
TRUNCATED_RAW_DEFLATE = bytes.fromhex("2b292acd4b4e2c494d51284a2c5748494dcb492c490500")


@dataclass(frozen=True)
class Limits:
    max_package_bytes: int = 8 * 1024 * 1024
    max_payload_bytes: int = 8 * 1024 * 1024
    max_zip_entries: int = 2048
    max_cumulative_inflated_bytes: int = 32 * 1024 * 1024
    max_name_bytes: int = 1024
    max_compression_ratio: int = 200
    ratio_guard_threshold_bytes: int = 1024 * 1024


DEFAULT_LIMITS = Limits()


@dataclass(frozen=True)
class EntrySpec:
    raw_name: bytes
    payload: bytes = b""
    method: int = 0
    flags: int = 0
    local_extra: bytes = b""
    compressed_payload: bytes | None = None
    declared_crc32: int | None = None
    declared_compressed_size: int | None = None
    declared_uncompressed_size: int | None = None
    descriptor: str | None = None


@dataclass(frozen=True)
class EntryLayout:
    local_offset: int
    data_offset: int
    central_offset: int


@dataclass(frozen=True)
class BuiltZip:
    data: bytes
    entries: tuple[EntrySpec, ...]
    layouts: tuple[EntryLayout, ...]


@dataclass(frozen=True)
class Fixture:
    case_id: str
    archive: bytes
    limits: Limits
    selector_raw_name: bytes
    selector_local_offset: int
    outcome: str
    error_code: str
    error_message: str
    expected_payload: bytes | None


def le16(value: int) -> bytes:
    return struct.pack("<H", value & 0xFFFF)


def le32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def raw_deflate(payload: bytes) -> bytes:
    compressor = zlib.compressobj(level=6, wbits=-15)
    return compressor.compress(payload) + compressor.flush()


def entry_metadata(entry: EntrySpec) -> tuple[int, int, int, bytes, int]:
    if entry.compressed_payload is not None:
        compressed = entry.compressed_payload
    elif entry.method == 8:
        compressed = raw_deflate(entry.payload)
    else:
        compressed = entry.payload
    crc32 = (binascii.crc32(entry.payload) & 0xFFFFFFFF
             if entry.declared_crc32 is None else entry.declared_crc32)
    compressed_size = (len(compressed) if entry.declared_compressed_size is None
                       else entry.declared_compressed_size)
    uncompressed_size = (len(entry.payload) if entry.declared_uncompressed_size is None
                         else entry.declared_uncompressed_size)
    flags = entry.flags | (0x0008 if entry.descriptor is not None else 0)
    return crc32, compressed_size, uncompressed_size, compressed, flags


def build_zip(entries: list[EntrySpec] | tuple[EntrySpec, ...]) -> BuiltZip:
    specs = tuple(entries)
    data = bytearray()
    partial: list[tuple[int, int]] = []
    for entry in specs:
        if entry.descriptor not in (None, "signed", "unsigned"):
            raise ValueError("descriptor must be signed, unsigned, or None")
        crc32, compressed_size, uncompressed_size, compressed, flags = entry_metadata(entry)
        if compressed_size != len(compressed):
            raise ValueError("declared compressed size must match the physical payload window")
        local_offset = len(data)
        uses_descriptor = entry.descriptor is not None
        data += le32(LOCAL_SIGNATURE)
        data += le16(20) + le16(flags) + le16(entry.method)
        data += le16(0) + le16(0)
        data += le32(0 if uses_descriptor else crc32)
        data += le32(0 if uses_descriptor else compressed_size)
        data += le32(0 if uses_descriptor else uncompressed_size)
        data += le16(len(entry.raw_name)) + le16(len(entry.local_extra))
        data += entry.raw_name + entry.local_extra
        data_offset = len(data)
        data += compressed
        if uses_descriptor:
            if entry.descriptor == "signed":
                data += le32(DESCRIPTOR_SIGNATURE)
            data += le32(crc32) + le32(compressed_size) + le32(uncompressed_size)
        partial.append((local_offset, data_offset))

    central_positions: list[int] = []
    central_offset = len(data)
    for entry, (local_offset, _) in zip(specs, partial):
        crc32, compressed_size, uncompressed_size, _, flags = entry_metadata(entry)
        central_positions.append(len(data))
        data += le32(CENTRAL_SIGNATURE)
        data += le16(20) + le16(20) + le16(flags) + le16(entry.method)
        data += le16(0) + le16(0)
        data += le32(crc32) + le32(compressed_size) + le32(uncompressed_size)
        data += le16(len(entry.raw_name)) + le16(0) + le16(0)
        data += le16(0) + le16(0) + le32(0) + le32(local_offset)
        data += entry.raw_name
    central_size = len(data) - central_offset
    data += le32(END_SIGNATURE)
    data += le16(0) + le16(0) + le16(len(specs)) + le16(len(specs))
    data += le32(central_size) + le32(central_offset) + le16(0)
    layouts = tuple(
        EntryLayout(local_offset, data_offset, central_positions[index])
        for index, (local_offset, data_offset) in enumerate(partial)
    )
    return BuiltZip(bytes(data), specs, layouts)


def selected_success(
    case_id: str,
    built: BuiltZip,
    selected_index: int,
    *,
    limits: Limits = DEFAULT_LIMITS,
) -> Fixture:
    entry = built.entries[selected_index]
    layout = built.layouts[selected_index]
    return Fixture(
        case_id,
        built.data,
        limits,
        entry.raw_name,
        layout.local_offset,
        "SUCCESS",
        "NONE",
        "NONE",
        entry.payload,
    )


def selected_failure(
    case_id: str,
    built: BuiltZip,
    selected_index: int,
    error_code: str,
    error_message: str,
    *,
    limits: Limits = DEFAULT_LIMITS,
) -> Fixture:
    entry = built.entries[selected_index]
    layout = built.layouts[selected_index]
    return Fixture(
        case_id,
        built.data,
        limits,
        entry.raw_name,
        layout.local_offset,
        "ERROR",
        error_code,
        error_message,
        None,
    )


def fixtures() -> list[Fixture]:
    result: list[Fixture] = []

    stored_payload = b"stored exact limit"
    stored = build_zip([EntrySpec(
        b"stored.nes",
        stored_payload,
        local_extra=b"\xfe\xca\x02\x00\x01\x02",
    )])
    result.append(selected_success(
        "stored_exact_limit_local_extra",
        stored,
        0,
        limits=replace(
            DEFAULT_LIMITS,
            max_payload_bytes=len(stored_payload),
            max_cumulative_inflated_bytes=len(stored_payload),
        ),
    ))

    large_payload = bytes((index * 73 + 19) & 0xFF for index in range(9001))
    if binascii.crc32(large_payload) & 0x80000000 == 0:
        raise AssertionError("large success payload must exercise a high-bit CRC32")
    large = build_zip([EntrySpec(
        b"large-deflate.nes",
        large_payload,
        method=8,
        compressed_payload=LARGE_RAW_DEFLATE,
        descriptor="signed",
    )])
    result.append(selected_success(
        "deflate_large_signed_descriptor_exact_limits",
        large,
        0,
        limits=replace(
            DEFAULT_LIMITS,
            max_payload_bytes=len(large_payload),
            max_cumulative_inflated_bytes=len(large_payload),
        ),
    ))

    empty = build_zip([EntrySpec(b"empty.nes", b"", method=8)])
    result.append(selected_success("deflate_empty", empty, 0))

    directory_payload = b"directory entries are selected like files"
    directory = build_zip([EntrySpec(
        b"folder/", directory_payload, method=8, compressed_payload=DIRECTORY_RAW_DEFLATE,
    )])
    result.append(selected_success("directory_payload_not_rejected", directory, 0))

    lazy = build_zip([
        EntrySpec(
            b"broken-sibling.nes",
            b"x",
            method=8,
            compressed_payload=b"\x07",
            declared_crc32=0,
            declared_uncompressed_size=1,
        ),
        EntrySpec(b"selected.nes", b"selected sibling remains lazy", method=8),
    ])
    result.append(selected_success("selected_only_bad_sibling", lazy, 1))

    int32_cap = 0x80000000
    payload_limit = build_zip([EntrySpec(
        b"payload-limit.nes",
        b"xx",
        method=8,
        compressed_payload=b"\x07",
        declared_crc32=0,
        declared_uncompressed_size=int32_cap,
    )])
    result.append(selected_failure(
        "payload_limit_precedes_invalid_decode",
        payload_limit,
        0,
        "PAYLOAD_LIMIT_EXCEEDED",
        "ZIP entry exceeds the payload limit",
        limits=replace(
            DEFAULT_LIMITS,
            max_payload_bytes=1,
            max_cumulative_inflated_bytes=int32_cap,
            ratio_guard_threshold_bytes=int32_cap,
        ),
    ))

    memory_cap = build_zip([EntrySpec(
        b"memory-cap.nes",
        b"x",
        method=8,
        compressed_payload=b"\x07",
        declared_crc32=0,
        declared_uncompressed_size=int32_cap,
    )])
    result.append(selected_failure(
        "int32_memory_cap_precedes_decode",
        memory_cap,
        0,
        "INFLATED_LIMIT_EXCEEDED",
        "ZIP entry cannot fit in memory",
        limits=replace(
            DEFAULT_LIMITS,
            max_payload_bytes=int32_cap,
            max_cumulative_inflated_bytes=int32_cap,
            ratio_guard_threshold_bytes=int32_cap,
        ),
    ))

    stored_mismatch = build_zip([EntrySpec(
        b"stored-mismatch.nes",
        b"abc",
        declared_uncompressed_size=4,
    )])
    result.append(selected_failure(
        "stored_sizes_differ",
        stored_mismatch,
        0,
        "INVALID_ZIP",
        "stored ZIP entry sizes differ",
    ))

    wrapped = build_zip([EntrySpec(
        b"zlib-wrapped.nes",
        b"wrapped",
        method=8,
        compressed_payload=zlib.compress(b"wrapped"),
    )])
    result.append(selected_failure(
        "zlib_wrapped_rejected_raw_only",
        wrapped,
        0,
        "INVALID_ZIP",
        "ZIP deflate stream is invalid",
    ))

    reserved = build_zip([EntrySpec(
        b"reserved-deflate.nes",
        b"x",
        method=8,
        compressed_payload=b"\x07",
    )])
    result.append(selected_failure(
        "reserved_deflate_block",
        reserved,
        0,
        "INVALID_ZIP",
        "ZIP deflate stream is invalid",
    ))

    truncated_payload = b"truncated raw deflate"
    truncated_bytes = TRUNCATED_RAW_DEFLATE
    if len(truncated_bytes) < 2:
        raise AssertionError("truncated fixture requires a multi-byte raw stream")
    truncated = build_zip([EntrySpec(
        b"truncated.nes",
        truncated_payload,
        method=8,
        compressed_payload=truncated_bytes[:-1],
    )])
    result.append(selected_failure(
        "truncated_deflate_incomplete",
        truncated,
        0,
        "INVALID_ZIP",
        "ZIP deflate stream ended before completion",
    ))

    trailing_payload = b"trailing"
    trailing = build_zip([EntrySpec(
        b"trailing.nes",
        trailing_payload,
        method=8,
        compressed_payload=raw_deflate(trailing_payload) + b"\x00",
        declared_crc32=0,
        declared_uncompressed_size=len(trailing_payload) + 1,
    )])
    result.append(selected_failure(
        "trailing_bytes_precede_size_and_crc",
        trailing,
        0,
        "INVALID_ZIP",
        "ZIP compressed size includes trailing bytes",
    ))

    over_declared_payload = b"abc"
    over_declared = build_zip([EntrySpec(
        b"over-declared.nes",
        over_declared_payload,
        method=8,
        declared_uncompressed_size=2,
    )])
    result.append(selected_failure(
        "actual_over_declared",
        over_declared,
        0,
        "INVALID_ZIP",
        "ZIP entry inflated past its declared size",
    ))

    cumulative_payload = b"xy"
    cumulative = build_zip([EntrySpec(
        b"cumulative-first.nes",
        cumulative_payload,
        method=8,
        declared_uncompressed_size=1,
    )])
    result.append(selected_failure(
        "actual_over_cumulative_precedes_declared",
        cumulative,
        0,
        "INFLATED_LIMIT_EXCEEDED",
        "ZIP inflated total exceeds the limit",
        limits=replace(DEFAULT_LIMITS, max_cumulative_inflated_bytes=1),
    ))

    under_declared_payload = b"under"
    under_declared = build_zip([EntrySpec(
        b"under-declared.nes",
        under_declared_payload,
        method=8,
        declared_crc32=0,
        declared_uncompressed_size=len(under_declared_payload) + 1,
    )])
    result.append(selected_failure(
        "actual_under_declared_precedes_crc",
        under_declared,
        0,
        "INVALID_ZIP",
        "ZIP inflated size differs from central metadata",
    ))

    crc_payload = b"coherent metadata but wrong CRC"
    crc_mismatch = build_zip([EntrySpec(
        b"crc-mismatch.nes",
        crc_payload,
        method=8,
        declared_crc32=0xFFFFFFFF,
    )])
    result.append(selected_failure(
        "coherent_metadata_crc_mismatch_high_bit",
        crc_mismatch,
        0,
        "INVALID_ZIP",
        "ZIP inflated CRC differs from central metadata",
    ))

    unsigned_descriptor = build_zip([EntrySpec(
        b"unsigned-descriptor.nes",
        b"unsigned descriptor payload",
        method=8,
        descriptor="unsigned",
    )])
    result.append(selected_success("descriptor_unsigned_read_success", unsigned_descriptor, 0))

    signed_descriptor = build_zip([EntrySpec(
        b"signed-descriptor.nes",
        b"signed descriptor payload",
        method=8,
        descriptor="signed",
    )])
    result.append(selected_success("descriptor_signed_read_success", signed_descriptor, 0))

    missing = build_zip([EntrySpec(b"present.nes", b"present")])
    result.append(Fixture(
        "exact_selector_missing",
        missing.data,
        DEFAULT_LIMITS,
        b"missing.nes",
        missing.layouts[0].local_offset,
        "ERROR",
        "ENTRY_MISSING",
        "ZIP exact locator is missing",
        None,
    ))

    if len(result) != 19:
        raise AssertionError(f"expected 19 fixtures, built {len(result)}")
    case_ids = [fixture.case_id for fixture in result]
    if len(set(case_ids)) != len(case_ids):
        raise AssertionError("fixture case IDs must be unique")
    unsafe = [value for value in case_ids if re.fullmatch(r"[a-z][a-z0-9_]*", value) is None]
    if unsafe:
        raise AssertionError(f"fixture case IDs are unsafe: {unsafe}")
    return result


def expected_corpus() -> dict[str, bytes]:
    columns = (
        "schema_version",
        "case_id",
        "blob",
        "sha256",
        "max_package_bytes",
        "max_payload_bytes",
        "max_zip_entries",
        "max_cumulative_inflated_bytes",
        "max_name_bytes",
        "max_compression_ratio",
        "ratio_guard_threshold_bytes",
        "selector_raw_name_hex",
        "selector_local_header_offset",
        "outcome",
        "error_code",
        "error_message",
        "payload_length",
        "payload_sha256",
    )
    rows = ["\t".join(columns)]
    contents: dict[str, bytes] = {}
    for fixture in fixtures():
        blob_name = fixture.case_id + ".zip"
        contents[blob_name] = fixture.archive
        limits = fixture.limits
        payload_length = ("NONE" if fixture.expected_payload is None
                          else str(len(fixture.expected_payload)))
        payload_sha256 = ("NONE" if fixture.expected_payload is None else
                          hashlib.sha256(fixture.expected_payload).hexdigest())
        row = (
            SCHEMA_VERSION,
            fixture.case_id,
            blob_name,
            hashlib.sha256(fixture.archive).hexdigest(),
            str(limits.max_package_bytes),
            str(limits.max_payload_bytes),
            str(limits.max_zip_entries),
            str(limits.max_cumulative_inflated_bytes),
            str(limits.max_name_bytes),
            str(limits.max_compression_ratio),
            str(limits.ratio_guard_threshold_bytes),
            fixture.selector_raw_name.hex(),
            str(fixture.selector_local_offset),
            fixture.outcome,
            fixture.error_code,
            fixture.error_message,
            payload_length,
            payload_sha256,
        )
        rows.append("\t".join(row))
    contents["manifest.tsv"] = ("\n".join(rows) + "\n").encode("ascii")
    return contents


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
    file_attributes = getattr(status, "st_file_attributes", 0)
    return stat.S_ISLNK(status.st_mode) or bool(file_attributes & reparse_attribute)


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
            if path.read_bytes() != expected_files[name]:
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
        prefix=f".{path.name}.",
        suffix=".tmp",
        dir=path.parent,
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
    print(f"generated {len(expected_files)} fixture corpus files in {OUTPUT_DIR}")
    return 0


def check(expected_files: dict[str, bytes]) -> int:
    diff = compare_corpus(expected_files)
    if not diff.is_empty():
        print_diff(diff)
        return 1
    print(f"fixture corpus is current: {len(expected_files)} files in {OUTPUT_DIR}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify the exact corpus without modifying any file",
    )
    arguments = parser.parse_args()
    corpus = expected_corpus()
    return check(corpus) if arguments.check else generate(corpus)


if __name__ == "__main__":
    raise SystemExit(main())
