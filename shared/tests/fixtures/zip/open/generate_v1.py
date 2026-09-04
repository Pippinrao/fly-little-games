#!/usr/bin/env python3
"""Generate the version-1 cross-language bounded ZIP-open fixture corpus."""

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
    central_extra: bytes = b""
    local_extra: bytes = b""
    comment: bytes = b""
    compressed_payload: bytes | None = None
    declared_crc32: int | None = None
    declared_compressed_size: int | None = None
    declared_uncompressed_size: int | None = None
    descriptor_signature: bool | None = None
    local_crc32: int | None = None
    local_compressed_size: int | None = None
    local_uncompressed_size: int | None = None
    disk_start: int = 0
    local_offset_override: int | None = None


@dataclass(frozen=True)
class EntryLayout:
    local_offset: int
    data_offset: int
    descriptor_offset: int
    entry_end: int
    central_offset: int


@dataclass
class BuiltZip:
    data: bytearray
    entries: tuple[EntrySpec, ...]
    layouts: tuple[EntryLayout, ...]
    central_offset: int
    central_size: int
    eocd_offset: int


@dataclass(frozen=True)
class Fixture:
    case_id: str
    payload: bytes
    limits: Limits
    outcome: str
    error_code: str
    error_message: str
    entries: tuple[str, ...]


def le16(value: int) -> bytes:
    return struct.pack("<H", value & 0xFFFF)


def le32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def put16(data: bytearray, offset: int, value: int) -> None:
    data[offset : offset + 2] = le16(value)


def put32(data: bytearray, offset: int, value: int) -> None:
    data[offset : offset + 4] = le32(value)


def raw_deflate(payload: bytes) -> bytes:
    compressor = zlib.compressobj(level=6, wbits=-15)
    return compressor.compress(payload) + compressor.flush()


def compressed_bytes(entry: EntrySpec) -> bytes:
    if entry.compressed_payload is not None:
        return entry.compressed_payload
    if entry.method == 8:
        return raw_deflate(entry.payload)
    return entry.payload


def metadata(entry: EntrySpec) -> tuple[int, int, int, bytes, int]:
    compressed = compressed_bytes(entry)
    crc32 = (binascii.crc32(entry.payload) & 0xFFFFFFFF
             if entry.declared_crc32 is None else entry.declared_crc32)
    compressed_size = (len(compressed) if entry.declared_compressed_size is None
                       else entry.declared_compressed_size)
    uncompressed_size = (len(entry.payload) if entry.declared_uncompressed_size is None
                         else entry.declared_uncompressed_size)
    flags = entry.flags | (0x0008 if entry.descriptor_signature is not None else 0)
    return crc32, compressed_size, uncompressed_size, compressed, flags


def build_zip(
    entries: list[EntrySpec] | tuple[EntrySpec, ...],
    *,
    central_order: list[int] | tuple[int, ...] | None = None,
    comment: bytes = b"",
) -> BuiltZip:
    specs = tuple(entries)
    if len(comment) > 0xFFFF:
        raise ValueError("EOCD comment exceeds the ZIP field")
    data = bytearray()
    partial_layouts: list[tuple[int, int, int, int]] = []
    for entry in specs:
        crc32, compressed_size, uncompressed_size, compressed, flags = metadata(entry)
        local_offset = len(data)
        descriptor = entry.descriptor_signature is not None
        local_crc = (0 if descriptor else crc32) if entry.local_crc32 is None else entry.local_crc32
        local_compressed = ((0 if descriptor else compressed_size)
                            if entry.local_compressed_size is None
                            else entry.local_compressed_size)
        local_uncompressed = ((0 if descriptor else uncompressed_size)
                              if entry.local_uncompressed_size is None
                              else entry.local_uncompressed_size)
        data += le32(LOCAL_SIGNATURE)
        data += le16(20) + le16(flags) + le16(entry.method)
        data += le16(0) + le16(0)
        data += le32(local_crc) + le32(local_compressed) + le32(local_uncompressed)
        data += le16(len(entry.raw_name)) + le16(len(entry.local_extra))
        data += entry.raw_name + entry.local_extra
        data_offset = len(data)
        data += compressed
        descriptor_offset = len(data)
        if descriptor:
            if entry.descriptor_signature:
                data += le32(DESCRIPTOR_SIGNATURE)
            data += le32(crc32) + le32(compressed_size) + le32(uncompressed_size)
        partial_layouts.append((local_offset, data_offset, descriptor_offset, len(data)))

    central_offset = len(data)
    central_positions = [-1] * len(specs)
    order = tuple(range(len(specs))) if central_order is None else tuple(central_order)
    if sorted(order) != list(range(len(specs))):
        raise ValueError("central order must contain every entry index exactly once")
    for index in order:
        entry = specs[index]
        crc32, compressed_size, uncompressed_size, _, flags = metadata(entry)
        central_positions[index] = len(data)
        local_offset = (partial_layouts[index][0]
                        if entry.local_offset_override is None
                        else entry.local_offset_override)
        data += le32(CENTRAL_SIGNATURE)
        data += le16(20) + le16(20) + le16(flags) + le16(entry.method)
        data += le16(0) + le16(0)
        data += le32(crc32) + le32(compressed_size) + le32(uncompressed_size)
        data += le16(len(entry.raw_name)) + le16(len(entry.central_extra))
        data += le16(len(entry.comment)) + le16(entry.disk_start)
        data += le16(0) + le32(0) + le32(local_offset)
        data += entry.raw_name + entry.central_extra + entry.comment
    central_size = len(data) - central_offset
    eocd_offset = len(data)
    data += le32(END_SIGNATURE)
    data += le16(0) + le16(0) + le16(len(specs)) + le16(len(specs))
    data += le32(central_size) + le32(central_offset) + le16(len(comment)) + comment

    layouts = tuple(
        EntryLayout(*partial_layouts[index], central_positions[index])
        for index in range(len(specs))
    )
    return BuiltZip(data, specs, layouts, central_offset, central_size, eocd_offset)


def clone(built: BuiltZip) -> BuiltZip:
    return BuiltZip(
        bytearray(built.data),
        built.entries,
        built.layouts,
        built.central_offset,
        built.central_size,
        built.eocd_offset,
    )


def encoded_entry(entry: EntrySpec, local_offset: int) -> str:
    crc32, compressed_size, uncompressed_size, _, flags = metadata(entry)
    raw_name = entry.raw_name.hex()
    central_extra = entry.central_extra.hex() or "-"
    directory = entry.raw_name.endswith((b"/", b"\\"))
    return "|".join((
        raw_name,
        central_extra,
        str(local_offset),
        str(flags),
        str(entry.method),
        str(crc32),
        str(compressed_size),
        str(uncompressed_size),
        str(directory).lower(),
    ))


def success(case_id: str, built: BuiltZip, limits: Limits = DEFAULT_LIMITS) -> Fixture:
    ordered = sorted(
        zip(built.entries, built.layouts),
        key=lambda value: value[1].local_offset,
    )
    return Fixture(
        case_id,
        bytes(built.data),
        limits,
        "SUCCESS",
        "NONE",
        "NONE",
        tuple(encoded_entry(entry, layout.local_offset) for entry, layout in ordered),
    )


def failure(
    case_id: str,
    payload: bytes | bytearray,
    message: str,
    *,
    code: str = "INVALID_ZIP",
    limits: Limits = DEFAULT_LIMITS,
) -> Fixture:
    return Fixture(case_id, bytes(payload), limits, "ERROR", code, message, ())


def fixtures() -> list[Fixture]:
    result: list[Fixture] = []

    stored = build_zip([
        EntrySpec(
            b"stored.nes",
            b"NES\x1a",
            central_extra=b"\xfe\xca\x02\x00\x01\x02",
            local_extra=b"\x34\x12\x00\x00",
        )
    ])
    result.append(success("valid_stored_metadata", stored))

    deflated = build_zip([EntrySpec(b"deflated.nes", b"deflate metadata", method=8)])
    result.append(success("valid_deflate_metadata", deflated))

    reversed_central = build_zip(
        [EntrySpec(b"first.nes", b"1"), EntrySpec(b"second.nes", b"22", method=8)],
        central_order=[1, 0],
        comment=b"c" * 0xFFFF,
    )
    result.append(success("valid_max_comment_central_reversed", reversed_central))

    package = build_zip([EntrySpec(b"package.nes", b"package")])
    result.append(success(
        "package_limit_exact",
        package,
        replace(DEFAULT_LIMITS, max_package_bytes=len(package.data)),
    ))
    result.append(failure(
        "package_limit_over",
        package.data,
        "ZIP package is over the source limit",
        code="PACKAGE_LIMIT_EXCEEDED",
        limits=replace(DEFAULT_LIMITS, max_package_bytes=len(package.data) - 1),
    ))

    two_entries = build_zip([EntrySpec(b"a", b"a"), EntrySpec(b"b", b"b")])
    result.append(success(
        "entry_count_exact",
        two_entries,
        replace(DEFAULT_LIMITS, max_zip_entries=2),
    ))
    three_entries = build_zip([
        EntrySpec(b"a", b"a"), EntrySpec(b"b", b"b"), EntrySpec(b"c", b"c")
    ])
    result.append(failure(
        "entry_count_over",
        three_entries.data,
        "ZIP entry count exceeds the limit",
        code="ENTRY_LIMIT_EXCEEDED",
        limits=replace(DEFAULT_LIMITS, max_zip_entries=2),
    ))

    sentinel = build_zip([])
    put16(sentinel.data, sentinel.eocd_offset + 8, 0xFFFF)
    put16(sentinel.data, sentinel.eocd_offset + 10, 0xFFFF)
    result.append(failure(
        "entry_count_sentinel_default_precedence",
        sentinel.data,
        "ZIP entry count exceeds the limit",
        code="ENTRY_LIMIT_EXCEEDED",
    ))
    result.append(failure(
        "entry_count_sentinel_zip64",
        sentinel.data,
        "ZIP64 archives are unsupported",
        limits=replace(DEFAULT_LIMITS, max_zip_entries=0xFFFF),
    ))

    cumulative = build_zip([EntrySpec(b"before", b"abc"), EntrySpec(b"after", b"defg")])
    result.append(success(
        "cumulative_exact_multiple",
        cumulative,
        replace(DEFAULT_LIMITS, max_cumulative_inflated_bytes=7),
    ))
    result.append(failure(
        "cumulative_over_multiple",
        cumulative.data,
        "ZIP inflated total exceeds the limit",
        code="INFLATED_LIMIT_EXCEEDED",
        limits=replace(DEFAULT_LIMITS, max_cumulative_inflated_bytes=6),
    ))

    name_exact = build_zip([EntrySpec(b"name", b"")])
    result.append(success(
        "name_limit_exact",
        name_exact,
        replace(DEFAULT_LIMITS, max_name_bytes=4),
    ))
    name_over = build_zip([EntrySpec(b"names", b"")])
    result.append(failure(
        "name_limit_over",
        name_over.data,
        "ZIP entry name exceeds the limit",
        code="NAME_LIMIT_EXCEEDED",
        limits=replace(DEFAULT_LIMITS, max_name_bytes=4),
    ))
    empty_name = build_zip([EntrySpec(b"", b"")])
    result.append(failure("empty_name", empty_name.data, "ZIP entry name is empty"))

    ratio_guard = replace(
        DEFAULT_LIMITS,
        max_cumulative_inflated_bytes=100,
        max_compression_ratio=3,
        ratio_guard_threshold_bytes=4,
    )
    ratio_exact_guard = build_zip([
        EntrySpec(
            b"guard",
            method=8,
            compressed_payload=b"",
            declared_crc32=0,
            declared_compressed_size=0,
            declared_uncompressed_size=4,
        )
    ])
    result.append(success("ratio_guard_exact_bypass", ratio_exact_guard, ratio_guard))
    ratio_above_guard = build_zip([
        EntrySpec(
            b"guard_plus",
            method=8,
            compressed_payload=b"x",
            declared_crc32=0,
            declared_compressed_size=1,
            declared_uncompressed_size=5,
        )
    ])
    result.append(failure(
        "ratio_guard_plus_one_checked",
        ratio_above_guard.data,
        "ZIP compression ratio exceeds the limit",
        code="RATIO_LIMIT_EXCEEDED",
        limits=ratio_guard,
    ))
    ratio_exact = build_zip([
        EntrySpec(
            b"ratio_exact",
            method=8,
            compressed_payload=b"xy",
            declared_crc32=0,
            declared_compressed_size=2,
            declared_uncompressed_size=6,
        )
    ])
    result.append(success("ratio_exact_pass", ratio_exact, ratio_guard))
    ratio_over = build_zip([
        EntrySpec(
            b"ratio_over",
            method=8,
            compressed_payload=b"xy",
            declared_crc32=0,
            declared_compressed_size=2,
            declared_uncompressed_size=7,
        )
    ])
    result.append(failure(
        "ratio_plus_one_fail",
        ratio_over.data,
        "ZIP compression ratio exceeds the limit",
        code="RATIO_LIMIT_EXCEEDED",
        limits=ratio_guard,
    ))
    ratio_zero = build_zip([
        EntrySpec(
            b"ratio_zero",
            method=8,
            compressed_payload=b"",
            declared_crc32=0,
            declared_compressed_size=0,
            declared_uncompressed_size=5,
        )
    ])
    result.append(failure(
        "ratio_zero_compressed_fail",
        ratio_zero.data,
        "ZIP compression ratio exceeds the limit",
        code="RATIO_LIMIT_EXCEEDED",
        limits=ratio_guard,
    ))

    result.append(failure("eocd_short", b"PK", "ZIP end record is missing"))
    result.append(failure(
        "eocd_missing",
        bytes(22),
        "ZIP end record is missing or truncated",
    ))
    result.append(failure(
        "eocd_misaligned",
        bytes(build_zip([]).data) + b"trailing",
        "ZIP end record is missing or truncated",
    ))

    fake_eocd_entry = EntrySpec(b"comment-eocd.nes", b"comment")
    fake_eocd_base = build_zip([fake_eocd_entry])
    fake_eocd_offset = fake_eocd_base.eocd_offset + 22
    fake_eocd_comment = (
        le32(END_SIGNATURE)
        + le16(0) + le16(0) + le16(0) + le16(0)
        + le32(0) + le32(fake_eocd_offset) + le16(0)
    )
    comment_with_fake_eocd = build_zip(
        [fake_eocd_entry],
        comment=fake_eocd_comment,
    )
    result.append(success("valid_comment_fake_eocd", comment_with_fake_eocd))

    prefixed = bytearray(b"junk") + stored.data
    prefix_length = 4
    put32(
        prefixed,
        prefix_length + stored.layouts[0].central_offset + 42,
        prefix_length + stored.layouts[0].local_offset,
    )
    put32(
        prefixed,
        prefix_length + stored.eocd_offset + 16,
        prefix_length + stored.central_offset,
    )
    result.append(failure(
        "prefix_junk",
        prefixed,
        "ZIP prefix bytes are unsupported",
    ))

    split = build_zip([])
    put16(split.data, split.eocd_offset + 4, 1)
    result.append(failure("split_archive", split.data, "split ZIP archives are unsupported"))

    zip64_size = build_zip([])
    put32(zip64_size.data, zip64_size.eocd_offset + 12, 0xFFFFFFFF)
    result.append(failure(
        "zip64_central_size", zip64_size.data, "ZIP64 archives are unsupported"
    ))
    zip64_offset = build_zip([])
    put32(zip64_offset.data, zip64_offset.eocd_offset + 16, 0xFFFFFFFF)
    result.append(failure(
        "zip64_central_offset", zip64_offset.data, "ZIP64 archives are unsupported"
    ))

    central_bounds = clone(stored)
    put32(central_bounds.data, central_bounds.eocd_offset + 12, central_bounds.central_size + 1)
    result.append(failure(
        "central_bounds", central_bounds.data, "ZIP central directory bounds are invalid"
    ))
    central_signature = clone(stored)
    central_signature.data[central_signature.central_offset] ^= 1
    result.append(failure(
        "central_signature",
        central_signature.data,
        "ZIP central directory signature is invalid",
    ))
    central_truncated = clone(stored)
    put16(central_truncated.data, central_truncated.eocd_offset + 8, 2)
    put16(central_truncated.data, central_truncated.eocd_offset + 10, 2)
    result.append(failure(
        "central_truncated",
        central_truncated.data,
        "ZIP central directory is truncated",
    ))
    central_entry_truncated = clone(stored)
    put16(
        central_entry_truncated.data,
        central_entry_truncated.central_offset + 28,
        len(stored.entries[0].raw_name) + 100,
    )
    result.append(failure(
        "central_entry_truncated",
        central_entry_truncated.data,
        "ZIP central entry is truncated",
    ))
    central_count = clone(stored)
    put16(central_count.data, central_count.eocd_offset + 8, 0)
    put16(central_count.data, central_count.eocd_offset + 10, 0)
    result.append(failure(
        "central_count", central_count.data, "ZIP central entry count is invalid"
    ))

    encrypted = build_zip([EntrySpec(b"encrypted", b"x", flags=0x0001)])
    result.append(failure(
        "encrypted",
        encrypted.data,
        "encrypted ZIP entries are unsupported",
        code="ENCRYPTED",
    ))
    unsupported = build_zip([EntrySpec(b"unsupported", b"x", method=12)])
    result.append(failure(
        "unsupported_method",
        unsupported.data,
        "ZIP compression method is unsupported",
        code="UNSUPPORTED_COMPRESSION",
    ))
    invalid_efs = build_zip([EntrySpec(b"\xc3(", b"x", flags=0x0800)])
    result.append(failure(
        "invalid_efs_utf8", invalid_efs.data, "ZIP entry name encoding is invalid"
    ))
    split_entry = build_zip([EntrySpec(b"split_entry", b"x", disk_start=1)])
    result.append(failure(
        "split_central_entry", split_entry.data, "split ZIP entries are unsupported"
    ))
    zip64_local = build_zip([
        EntrySpec(b"zip64_local", b"x", local_offset_override=0xFFFFFFFF)
    ])
    result.append(failure(
        "zip64_local_offset", zip64_local.data, "ZIP64 local headers are unsupported"
    ))

    local_truncated = clone(stored)
    put32(
        local_truncated.data,
        local_truncated.layouts[0].central_offset + 42,
        len(local_truncated.data) - 10,
    )
    result.append(failure(
        "local_header_truncated", local_truncated.data, "ZIP local header is truncated"
    ))
    local_signature = clone(stored)
    local_signature.data[local_signature.layouts[0].local_offset] ^= 1
    result.append(failure(
        "local_signature", local_signature.data, "ZIP local header signature is invalid"
    ))
    local_metadata = clone(stored)
    put16(local_metadata.data, local_metadata.layouts[0].local_offset + 28, 0xFFFF)
    result.append(failure(
        "local_metadata_truncated",
        local_metadata.data,
        "ZIP local entry metadata is truncated",
    ))
    local_flags = clone(stored)
    put16(local_flags.data, local_flags.layouts[0].local_offset + 6, 0x0010)
    result.append(failure(
        "local_flags_mismatch",
        local_flags.data,
        "ZIP local and central flags or methods differ",
    ))
    local_method = clone(stored)
    put16(local_method.data, local_method.layouts[0].local_offset + 8, 8)
    result.append(failure(
        "local_method_mismatch",
        local_method.data,
        "ZIP local and central flags or methods differ",
    ))
    local_name_length = clone(stored)
    put16(
        local_name_length.data,
        local_name_length.layouts[0].local_offset + 26,
        len(stored.entries[0].raw_name) - 1,
    )
    result.append(failure(
        "local_name_length_mismatch",
        local_name_length.data,
        "ZIP local and central entry names differ",
    ))
    local_name_bytes = clone(stored)
    local_name_bytes.data[local_name_bytes.layouts[0].local_offset + 30] ^= 1
    result.append(failure(
        "local_name_bytes_mismatch",
        local_name_bytes.data,
        "ZIP local and central entry names differ",
    ))
    local_crc = clone(stored)
    put32(local_crc.data, local_crc.layouts[0].local_offset + 14, 1)
    result.append(failure(
        "local_crc_mismatch",
        local_crc.data,
        "ZIP local and central CRC or sizes differ",
    ))
    local_compressed = clone(stored)
    put32(
        local_compressed.data,
        local_compressed.layouts[0].local_offset + 18,
        metadata(stored.entries[0])[1] + 1,
    )
    result.append(failure(
        "local_compressed_size_mismatch",
        local_compressed.data,
        "ZIP local and central CRC or sizes differ",
    ))
    local_uncompressed = clone(stored)
    put32(
        local_uncompressed.data,
        local_uncompressed.layouts[0].local_offset + 22,
        metadata(stored.entries[0])[2] + 1,
    )
    result.append(failure(
        "local_uncompressed_size_mismatch",
        local_uncompressed.data,
        "ZIP local and central CRC or sizes differ",
    ))
    payload_truncated = clone(stored)
    impossible_size = payload_truncated.central_offset
    put32(payload_truncated.data, payload_truncated.layouts[0].local_offset + 18, impossible_size)
    put32(payload_truncated.data, payload_truncated.layouts[0].central_offset + 20, impossible_size)
    result.append(failure(
        "compressed_payload_truncated",
        payload_truncated.data,
        "ZIP compressed payload is truncated",
    ))

    descriptor_builds: dict[tuple[int, bool], BuiltZip] = {}
    for method, method_name in ((0, "stored"), (8, "deflate")):
        for signature, signature_name in ((False, "unsigned"), (True, "signed")):
            descriptor_zip = build_zip([
                EntrySpec(
                    b"descriptor.nes",
                    b"descriptor-payload",
                    method=method,
                    descriptor_signature=signature,
                )
            ])
            descriptor_builds[(method, signature)] = descriptor_zip
            result.append(success(
                f"descriptor_{method_name}_{signature_name}", descriptor_zip
            ))

    collision_payload = bytes.fromhex("ac0a7ad5")
    if binascii.crc32(collision_payload) & 0xFFFFFFFF != DESCRIPTOR_SIGNATURE:
        raise AssertionError("descriptor collision payload no longer has the required CRC32")
    for signature, signature_name in ((False, "unsigned"), (True, "signed")):
        collision_zip = build_zip([
            EntrySpec(
                b"descriptor-collision.nes",
                collision_payload,
                descriptor_signature=signature,
            ),
            EntrySpec(b"after-collision.nes", b"after"),
        ])
        result.append(success(
            f"descriptor_crc_signature_{signature_name}", collision_zip
        ))

    descriptor_equal_entry = EntrySpec(
        b"descriptor_equal",
        b"equal",
        method=8,
        descriptor_signature=True,
    )
    equal_crc, equal_compressed, equal_uncompressed, _, _ = metadata(descriptor_equal_entry)
    descriptor_equal = build_zip([
        replace(
            descriptor_equal_entry,
            local_crc32=equal_crc,
            local_compressed_size=equal_compressed,
            local_uncompressed_size=equal_uncompressed,
        )
    ])
    result.append(success("descriptor_local_equal_metadata", descriptor_equal))

    descriptor_base = descriptor_builds[(0, False)]
    for case_id, field_offset, message in (
        ("descriptor_local_crc_mismatch", 14, "ZIP local and central CRC differ"),
        ("descriptor_local_compressed_mismatch", 18,
         "ZIP local and central compressed size differ"),
        ("descriptor_local_size_mismatch", 22, "ZIP local and central size differ"),
    ):
        mismatched = clone(descriptor_base)
        put32(mismatched.data, mismatched.layouts[0].local_offset + field_offset, 1)
        result.append(failure(case_id, mismatched.data, message))

    descriptor_truncated = clone(descriptor_builds[(0, True)])
    descriptor_offset = descriptor_truncated.layouts[0].descriptor_offset
    del descriptor_truncated.data[descriptor_offset + 7 : descriptor_offset + 16]
    shifted_eocd = descriptor_truncated.eocd_offset - 9
    put32(descriptor_truncated.data, shifted_eocd + 16, descriptor_offset + 7)
    result.append(failure(
        "descriptor_truncated",
        descriptor_truncated.data,
        "ZIP data descriptor differs from central metadata",
    ))

    descriptor_mismatch = clone(descriptor_builds[(8, True)])
    put32(
        descriptor_mismatch.data,
        descriptor_mismatch.layouts[0].descriptor_offset + 4,
        1,
    )
    result.append(failure(
        "descriptor_mismatched",
        descriptor_mismatch.data,
        "ZIP data descriptor differs from central metadata",
    ))

    overlap_uncompressed = CENTRAL_SIGNATURE
    descriptor_overlap = build_zip([
        EntrySpec(
            b"descriptor_overlap",
            method=8,
            compressed_payload=b"",
            declared_crc32=0,
            declared_compressed_size=0,
            declared_uncompressed_size=overlap_uncompressed,
            descriptor_signature=False,
        )
    ])
    overlap_descriptor = descriptor_overlap.layouts[0].descriptor_offset
    del descriptor_overlap.data[overlap_descriptor + 8 : overlap_descriptor + 12]
    shifted_eocd = descriptor_overlap.eocd_offset - 4
    put32(descriptor_overlap.data, shifted_eocd + 16, overlap_descriptor + 8)
    result.append(failure(
        "descriptor_overlaps_central",
        descriptor_overlap.data,
        "ZIP data descriptor overlaps the central directory",
        limits=replace(
            DEFAULT_LIMITS,
            max_cumulative_inflated_bytes=overlap_uncompressed,
            ratio_guard_threshold_bytes=overlap_uncompressed,
        ),
    ))

    local_overlap = build_zip([EntrySpec(b"outer", b"x"), EntrySpec(b"inner", b"y")])
    first = local_overlap.layouts[0]
    overlapping_size = local_overlap.central_offset - first.data_offset
    put32(local_overlap.data, first.local_offset + 18, overlapping_size)
    put32(local_overlap.data, first.local_offset + 22, overlapping_size)
    put32(local_overlap.data, first.central_offset + 20, overlapping_size)
    put32(local_overlap.data, first.central_offset + 24, overlapping_size)
    result.append(failure(
        "local_entries_overlap", local_overlap.data, "ZIP local entries overlap"
    ))

    duplicates = build_zip([
        EntrySpec(b"same.nes", b"first"),
        EntrySpec(b"same.nes", b"second", central_extra=b"\x01\x00\x00\x00"),
    ])
    result.append(success("duplicate_raw_names", duplicates))

    directories = build_zip([
        EntrySpec(b"slash/", b""),
        EntrySpec(b"back\\", b""),
        EntrySpec(b"ordinary", b""),
    ])
    result.append(success("directory_suffixes", directories))

    if len(result) != 64:
        raise AssertionError(f"expected 64 fixtures, built {len(result)}")
    case_ids = [fixture.case_id for fixture in result]
    if len(set(case_ids)) != len(case_ids):
        raise AssertionError("fixture case IDs must be unique")
    unsafe = [case_id for case_id in case_ids if re.fullmatch(r"[a-z][a-z0-9_]*", case_id) is None]
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
        "outcome",
        "error_code",
        "error_message",
        "entries",
    )
    rows = ["\t".join(columns)]
    contents: dict[str, bytes] = {}
    for fixture in fixtures():
        blob_name = fixture.case_id + ".zip"
        contents[blob_name] = fixture.payload
        limits = fixture.limits
        row = (
            SCHEMA_VERSION,
            fixture.case_id,
            blob_name,
            hashlib.sha256(fixture.payload).hexdigest(),
            str(limits.max_package_bytes),
            str(limits.max_payload_bytes),
            str(limits.max_zip_entries),
            str(limits.max_cumulative_inflated_bytes),
            str(limits.max_name_bytes),
            str(limits.max_compression_ratio),
            str(limits.ratio_guard_threshold_bytes),
            fixture.outcome,
            fixture.error_code,
            fixture.error_message,
            ";".join(fixture.entries) if fixture.entries else "NONE",
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
