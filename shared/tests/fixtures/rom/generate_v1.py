#!/usr/bin/env python3
"""Generate the version-1 cross-platform ROM parser fixture corpus."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass
from pathlib import Path


SCHEMA_VERSION = "1"
FDS_SIDE_BYTES = 65_500
FDS_SIDE_SIGNATURE = b"\x01*NINTENDO-HVC*"
OUTPUT_DIR = Path(__file__).resolve().parent / "v1"


@dataclass(frozen=True)
class Expected:
    recognized: bool
    format: str
    state: str
    reason: str
    expected_bytes: int
    actual_bytes: int
    prg_bytes: int
    chr_bytes: int
    mapper: int
    submapper: int
    trainer: bool
    battery: bool
    disk_sides: int
    warnings: tuple[str, ...] = ()


@dataclass(frozen=True)
class Fixture:
    case_id: str
    payload: bytes
    expected: Expected


def ines(
    prg_units: int,
    chr_units: int,
    flags6: int = 0,
    flags7: int = 0,
    *,
    trainer: bool = False,
    dirty: bool = False,
    trailing: int = 0,
) -> bytes:
    actual_flags6 = flags6 | 0x04 if trainer else flags6 & ~0x04
    expected = 16 + (512 if trainer else 0) + prg_units * 16_384 + chr_units * 8_192
    data = bytearray(expected + trailing)
    data[0:4] = b"NES\x1a"
    data[4] = prg_units
    data[5] = chr_units
    data[6] = actual_flags6
    data[7] = flags7
    if dirty:
        data[12] = 1
    return bytes(data)


def nes2_standard(*, trailing: int = 0) -> bytes:
    prg_bytes = 257 * 16_384
    chr_bytes = 8_192
    data = bytearray(16 + 512 + prg_bytes + chr_bytes + trailing)
    data[0:4] = b"NES\x1a"
    data[4] = 1
    data[5] = 1
    data[6] = 0x34  # mapper low nibble 3 and trainer
    data[7] = 0x28  # NES2 marker and mapper middle nibble 2
    data[8] = 0xA1  # submapper A and mapper high nibble 1
    data[9] = 0x01  # PRG high size nibble 1 and CHR high size nibble 0
    return bytes(data)


def nes2_exponential_header(
    prg_exponent: int,
    prg_multiplier_bits: int,
    chr_exponent: int,
    chr_multiplier_bits: int,
) -> bytearray:
    data = bytearray(16)
    data[0:4] = b"NES\x1a"
    data[4] = (prg_exponent << 2) | prg_multiplier_bits
    data[5] = (chr_exponent << 2) | chr_multiplier_bits
    data[7] = 0x08
    data[9] = 0xFF
    return data


def nes2_exponential_valid() -> bytes:
    data = nes2_exponential_header(6, 0, 5, 1)
    data.extend(bytes(64 + 96))
    return bytes(data)


def headered_fds(sides: int, *, trailing: int = 0) -> bytes:
    data = bytearray(16 + sides * FDS_SIDE_BYTES + trailing)
    data[0:4] = b"FDS\x1a"
    data[4] = sides
    for side in range(sides):
        offset = 16 + side * FDS_SIDE_BYTES
        data[offset : offset + len(FDS_SIDE_SIGNATURE)] = FDS_SIDE_SIGNATURE
    return bytes(data)


def headerless_fds(sides: int) -> bytes:
    data = bytearray(sides * FDS_SIDE_BYTES)
    for side in range(sides):
        offset = side * FDS_SIDE_BYTES
        data[offset : offset + len(FDS_SIDE_SIGNATURE)] = FDS_SIDE_SIGNATURE
    return bytes(data)


def unif_header() -> bytes:
    return b"UNIF" + bytes(28)


def expected(
    payload: bytes,
    format: str,
    state: str,
    reason: str,
    *,
    expected_bytes: int | None = None,
    prg_bytes: int = 0,
    chr_bytes: int = 0,
    mapper: int = -1,
    submapper: int = -1,
    trainer: bool = False,
    battery: bool = False,
    disk_sides: int = 0,
    warnings: tuple[str, ...] = (),
) -> Expected:
    return Expected(
        True,
        format,
        state,
        reason,
        len(payload) if expected_bytes is None else expected_bytes,
        len(payload),
        prg_bytes,
        chr_bytes,
        mapper,
        submapper,
        trainer,
        battery,
        disk_sides,
        warnings,
    )


def fixtures() -> list[Fixture]:
    playable = ines(1, 0, flags6=0x12, flags7=0x40)
    dirty = ines(
        1,
        0,
        flags6=0xA2,
        flags7=0xB0,
        trainer=True,
        dirty=True,
        trailing=2,
    )
    short_nes = b"NES\x1a"
    zero_prg = ines(0, 0)
    truncated_nes = ines(1, 0)[:32]
    extended_nes2 = nes2_standard(trailing=3)
    exponential_nes2 = nes2_exponential_valid()
    shift_overflow = bytes(nes2_exponential_header(63, 0, 0, 0))
    multiply_overflow = bytes(nes2_exponential_header(62, 3, 0, 0))
    total_overflow = bytes(nes2_exponential_header(62, 0, 62, 0))

    valid_headered_fds = headered_fds(1)
    trailing_headered_fds = headered_fds(1, trailing=3)
    valid_headerless_fds = headerless_fds(1)
    zero_side_fds = headered_fds(0)
    truncated_headered_fds = headered_fds(1)[:100]
    truncated_headerless_fds = headerless_fds(1)[:100]
    invalid_headered_fds = bytearray(headered_fds(1))
    invalid_headered_fds[16] = 2
    invalid_second_headerless_fds = bytearray(headerless_fds(2))
    invalid_second_headerless_fds[FDS_SIDE_BYTES] = 2

    valid_unif = unif_header() + b"PRGa" + (4).to_bytes(4, "little") + b"\x01\x02\x03\x04"
    missing_prg_unif = unif_header()
    truncated_chunk_header_unif = unif_header() + b"PRG0"
    truncated_chunk_data_unif = unif_header() + b"PRG0" + (4).to_bytes(4, "little")
    unsigned_chunk_length_unif = unif_header() + b"DATA" + b"\xff\xff\xff\xff"
    short_unif = b"UNIF"
    unknown = b"\x00\x01\x02\x03\x04"

    return [
        Fixture(
            "playable_ines_metadata",
            playable,
            expected(
                playable,
                "INES",
                "PLAYABLE",
                "PLAYABLE_NES",
                prg_bytes=16_384,
                mapper=0x41,
                submapper=0,
                battery=True,
            ),
        ),
        Fixture(
            "dirty_header_ines",
            dirty,
            expected(
                dirty,
                "INES",
                "PLAYABLE",
                "PLAYABLE_NES",
                expected_bytes=16_912,
                prg_bytes=16_384,
                mapper=0x0A,
                submapper=0,
                trainer=True,
                battery=True,
                warnings=("DIRTY_HEADER", "TRAILING_DATA"),
            ),
        ),
        Fixture(
            "nes_header_too_short",
            short_nes,
            expected(short_nes, "INES", "INVALID", "NES_HEADER_INVALID"),
        ),
        Fixture(
            "zero_prg_ines",
            zero_prg,
            expected(
                zero_prg,
                "INES",
                "INVALID",
                "NES_ZERO_PRG",
                mapper=0,
                submapper=0,
            ),
        ),
        Fixture(
            "truncated_ines",
            truncated_nes,
            expected(
                truncated_nes,
                "INES",
                "INVALID",
                "NES_TRUNCATED",
                expected_bytes=16_400,
                prg_bytes=16_384,
                mapper=0,
                submapper=0,
            ),
        ),
        Fixture(
            "nes2_extended_mapper_metadata",
            extended_nes2,
            expected(
                extended_nes2,
                "NES2",
                "PLAYABLE",
                "PLAYABLE_NES",
                expected_bytes=4_219_408,
                prg_bytes=4_210_688,
                chr_bytes=8_192,
                mapper=0x123,
                submapper=0xA,
                trainer=True,
                warnings=("TRAILING_DATA",),
            ),
        ),
        Fixture(
            "nes2_exponential_64_96",
            exponential_nes2,
            expected(
                exponential_nes2,
                "NES2",
                "PLAYABLE",
                "PLAYABLE_NES",
                prg_bytes=64,
                chr_bytes=96,
                mapper=0,
                submapper=0,
            ),
        ),
        Fixture(
            "nes2_exponential_shift_overflow",
            shift_overflow,
            expected(
                shift_overflow,
                "NES2",
                "INVALID",
                "NES_SIZE_OVERFLOW",
                expected_bytes=0,
                mapper=0,
                submapper=0,
            ),
        ),
        Fixture(
            "nes2_exponential_multiplier_overflow",
            multiply_overflow,
            expected(
                multiply_overflow,
                "NES2",
                "INVALID",
                "NES_SIZE_OVERFLOW",
                expected_bytes=0,
                mapper=0,
                submapper=0,
            ),
        ),
        Fixture(
            "nes2_exponential_total_overflow",
            total_overflow,
            expected(
                total_overflow,
                "NES2",
                "INVALID",
                "NES_SIZE_OVERFLOW",
                expected_bytes=0,
                prg_bytes=1 << 62,
                chr_bytes=1 << 62,
                mapper=0,
                submapper=0,
            ),
        ),
        Fixture(
            "valid_headered_fds",
            valid_headered_fds,
            expected(
                valid_headered_fds,
                "FDS",
                "UNSUPPORTED",
                "FDS_BIOS_API_NOT_IMPLEMENTED",
                disk_sides=1,
            ),
        ),
        Fixture(
            "headered_fds_trailing_data",
            trailing_headered_fds,
            expected(
                trailing_headered_fds,
                "FDS",
                "UNSUPPORTED",
                "FDS_BIOS_API_NOT_IMPLEMENTED",
                expected_bytes=65_516,
                disk_sides=1,
                warnings=("TRAILING_DATA",),
            ),
        ),
        Fixture(
            "valid_headerless_fds",
            valid_headerless_fds,
            expected(
                valid_headerless_fds,
                "FDS",
                "UNSUPPORTED",
                "FDS_BIOS_API_NOT_IMPLEMENTED",
                disk_sides=1,
            ),
        ),
        Fixture(
            "zero_side_fds",
            zero_side_fds,
            expected(zero_side_fds, "FDS", "INVALID", "FDS_INVALID_SIDE_COUNT"),
        ),
        Fixture(
            "truncated_headered_fds",
            truncated_headered_fds,
            expected(
                truncated_headered_fds,
                "FDS",
                "INVALID",
                "FDS_TRUNCATED",
                expected_bytes=65_516,
                disk_sides=1,
            ),
        ),
        Fixture(
            "truncated_headerless_fds",
            truncated_headerless_fds,
            expected(
                truncated_headerless_fds,
                "FDS",
                "INVALID",
                "FDS_TRUNCATED",
                expected_bytes=65_500,
                disk_sides=1,
            ),
        ),
        Fixture(
            "invalid_headered_fds_signature",
            bytes(invalid_headered_fds),
            expected(
                bytes(invalid_headered_fds),
                "FDS",
                "INVALID",
                "FDS_INVALID_HEADER",
                disk_sides=1,
            ),
        ),
        Fixture(
            "invalid_second_headerless_fds_signature",
            bytes(invalid_second_headerless_fds),
            expected(
                bytes(invalid_second_headerless_fds),
                "FDS",
                "INVALID",
                "FDS_INVALID_HEADER",
                disk_sides=2,
            ),
        ),
        Fixture(
            "valid_unif_positive_prg",
            valid_unif,
            expected(
                valid_unif,
                "UNIF",
                "UNSUPPORTED",
                "UNIF_PRODUCT_DISABLED",
            ),
        ),
        Fixture(
            "unif_missing_prg",
            missing_prg_unif,
            expected(missing_prg_unif, "UNIF", "INVALID", "UNIF_MISSING_PRG"),
        ),
        Fixture(
            "unif_truncated_chunk_header",
            truncated_chunk_header_unif,
            expected(
                truncated_chunk_header_unif,
                "UNIF",
                "INVALID",
                "UNIF_INVALID_CHUNK",
            ),
        ),
        Fixture(
            "unif_truncated_chunk_data",
            truncated_chunk_data_unif,
            expected(
                truncated_chunk_data_unif,
                "UNIF",
                "INVALID",
                "UNIF_INVALID_CHUNK",
            ),
        ),
        Fixture(
            "unif_unsigned_chunk_length",
            unsigned_chunk_length_unif,
            expected(
                unsigned_chunk_length_unif,
                "UNIF",
                "INVALID",
                "UNIF_INVALID_CHUNK",
            ),
        ),
        Fixture(
            "unif_header_too_short",
            short_unif,
            expected(short_unif, "UNIF", "INVALID", "UNIF_INVALID_CHUNK"),
        ),
        Fixture(
            "unknown_bytes",
            unknown,
            Expected(
                False,
                "UNKNOWN",
                "UNKNOWN",
                "UNKNOWN_FORMAT",
                len(unknown),
                len(unknown),
                0,
                0,
                -1,
                -1,
                False,
                False,
                0,
            ),
        ),
    ]


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    columns = (
        "schema_version",
        "case_id",
        "blob",
        "sha256",
        "recognized",
        "format",
        "state",
        "reason",
        "expected_bytes",
        "actual_bytes",
        "prg_bytes",
        "chr_bytes",
        "mapper",
        "submapper",
        "trainer",
        "battery",
        "disk_sides",
        "warnings",
    )
    rows = ["\t".join(columns)]
    for fixture in fixtures():
        blob_name = fixture.case_id + ".bin"
        (OUTPUT_DIR / blob_name).write_bytes(fixture.payload)
        value = fixture.expected
        row = (
            SCHEMA_VERSION,
            fixture.case_id,
            blob_name,
            hashlib.sha256(fixture.payload).hexdigest(),
            str(value.recognized).lower(),
            value.format,
            value.state,
            value.reason,
            str(value.expected_bytes),
            str(value.actual_bytes),
            str(value.prg_bytes),
            str(value.chr_bytes),
            str(value.mapper),
            str(value.submapper),
            str(value.trainer).lower(),
            str(value.battery).lower(),
            str(value.disk_sides),
            ",".join(value.warnings) if value.warnings else "NONE",
        )
        rows.append("\t".join(row))
    (OUTPUT_DIR / "manifest.tsv").write_bytes(("\n".join(rows) + "\n").encode("ascii"))


if __name__ == "__main__":
    main()
