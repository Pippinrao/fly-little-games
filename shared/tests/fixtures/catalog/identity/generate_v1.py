#!/usr/bin/env python3
"""Generate the version-1 cross-language content-identity fixture corpus."""

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
from dataclasses import dataclass
from pathlib import Path


SCHEMA_VERSION = "1"
OUTPUT_DIR = Path(__file__).resolve().parent / "v1"
HASH_HEADER = (
    "schema_version",
    "case_id",
    "blob",
    "blob_size",
    "blob_sha256",
    "expected_sha1",
    "expected_sha256",
    "expected_crc32",
)
STABLE_HEADER = (
    "schema_version",
    "case_id",
    "operation",
    "arg0_utf8_hex",
    "arg1_utf8_hex",
    "arg2_utf8_hex",
    "outcome",
    "expected_value",
    "expected_message",
)
OPERATIONS = {
    "package_id": 2,
    "saf_source_id": 1,
    "variant_id": 3,
    "provisional_game_id": 1,
    "entry_outcome_id": 2,
}
BLANK_CODE_POINTS = frozenset(
    list(range(0x0009, 0x000E))
    + list(range(0x001C, 0x0021))
    + [0x00A0, 0x1680]
    + list(range(0x2000, 0x200B))
    + [0x2028, 0x2029, 0x202F, 0x205F, 0x3000]
)


@dataclass(frozen=True)
class StableCase:
    case_id: str
    operation: str
    args: tuple[bytes, ...]
    outcome: str
    expected_value: str
    expected_message: str


def framed(value: bytes) -> bytes:
    if len(value) > 0x7FFFFFFF:
        raise ValueError("ID input is too long")
    return struct.pack(">I", len(value)) + value


def is_blank(value: bytes) -> bool:
    decoded = value.decode("utf-8", errors="strict")
    return not decoded or all(ord(character) in BLANK_CODE_POINTS for character in decoded)


def normalized_sha256(value: bytes) -> str:
    decoded = value.decode("utf-8", errors="strict")
    utf16_units = sum(2 if ord(character) > 0xFFFF else 1 for character in decoded)
    if utf16_units != 64:
        raise ValueError("payload SHA-256 must be 64 hex characters")
    if re.fullmatch(r"[0-9A-Fa-f]{64}", decoded) is None:
        raise ValueError("payload SHA-256 must be hexadecimal")
    return decoded.upper()


def checked_id_input(value: bytes) -> bytes:
    try:
        blank = is_blank(value)
    except UnicodeDecodeError as error:
        raise ValueError("ID input must be valid Unicode") from error
    if blank:
        raise ValueError("ID input must not be blank")
    return value


def digest_parts(*values: bytes) -> str:
    digest = hashlib.sha256()
    for value in values:
        digest.update(framed(checked_id_input(value)))
    return digest.hexdigest().upper()


def evaluate(operation: str, args: tuple[bytes, ...]) -> str:
    if operation == "package_id":
        return "pkg:" + digest_parts(args[0], args[1])
    if operation == "saf_source_id":
        return "source:" + digest_parts(b"SAF_TREE", args[0])
    if operation == "variant_id":
        # This normalization deliberately precedes validation of the framed ID inputs.
        normalized = normalized_sha256(args[2]).encode("ascii")
        return "variant:" + digest_parts(args[0], args[1], normalized)
    if operation == "provisional_game_id":
        return "game:" + normalized_sha256(args[0])
    if operation == "entry_outcome_id":
        return "entry:" + digest_parts(args[0], args[1])
    raise AssertionError(f"unknown operation: {operation}")


def success(case_id: str, operation: str, *args: bytes) -> StableCase:
    arguments = tuple(args)
    return StableCase(case_id, operation, arguments, "SUCCESS", evaluate(operation, arguments), "NONE")


def failure(
    case_id: str,
    operation: str,
    expected_message: str,
    *args: bytes,
) -> StableCase:
    arguments = tuple(args)
    try:
        evaluate(operation, arguments)
    except (UnicodeDecodeError, ValueError) as error:
        actual = str(error)
        if actual != expected_message:
            raise AssertionError(
                f"{case_id}: expected error {expected_message!r}, got {actual!r}"
            ) from error
    else:
        raise AssertionError(f"{case_id}: expected identity calculation to fail")
    return StableCase(case_id, operation, arguments, "ERROR", "NONE", expected_message)


def hash_blobs() -> tuple[tuple[str, bytes], ...]:
    result = (
        ("empty", b""),
        ("abc", b"abc"),
        ("binary_00_ff", bytes(range(256))),
        ("pad_55", b"a" * 55),
        ("pad_56", b"a" * 56),
        ("pad_63", b"a" * 63),
        ("pad_64", b"a" * 64),
        ("pad_65", b"a" * 65),
        ("million_a", b"a" * 1_000_000),
        ("digits_123456789", b"123456789"),
        ("ownership_payload", b"payload bytes owned by the caller"),
        ("ownership_physical", b"physical package bytes differ from payload"),
    )
    assert_known_hash(
        "empty",
        result[0][1],
        "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709",
        "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
        "00000000",
    )
    assert_known_hash(
        "binary_00_ff",
        result[2][1],
        "4916D6BDB7F78E6803698CAB32D1586EA457DFC8",
        "40AFF2E9D2D8922E47AFD4648E6967497158785FBD1DA870E7110266BF944880",
        "29058C73",
    )
    assert_known_hash(
        "million_a",
        result[8][1],
        "34AA973CD4C4DAA4F61EEB2BDBAD27316534016F",
        "CDC76E5C9914FB9281A1C7E284D73E67F1809A48A497200E046D39CCC7112CD0",
        "DC25BFBC",
    )
    assert_known_hash(
        "digits_123456789",
        result[9][1],
        "F7C3BC1D808E04732ADF679965CCC34CA7AE3441",
        "15E2B0D3C33891EBB0F1EF609EC419420C20E320CE94C65FBC8C3312448EB225",
        "CBF43926",
    )
    return result


def assert_known_hash(case_id: str, blob: bytes, sha1: str, sha256: str, crc32: str) -> None:
    actual = (
        hashlib.sha1(blob).hexdigest().upper(),
        hashlib.sha256(blob).hexdigest().upper(),
        f"{binascii.crc32(blob) & 0xFFFFFFFF:08X}",
    )
    expected = (sha1, sha256, crc32)
    if actual != expected:
        raise AssertionError(f"{case_id}: Python oracle disagrees with frozen KAT")


def stable_cases() -> tuple[StableCase, ...]:
    package_id = b"pkg:99BDAC5F521F764F18A3D37CF0A700AE7FCBC43F008677B49089BAD0A5AA3C37"
    payload_hash_lower = b"239f59ed55e737c77147cf55ad0c1b030b6d7ee748a7426952f9b852d5a935e5"
    fullwidth_hash = ("０" * 64).encode("utf-8")
    arabic_hash = ("٠" * 64).encode("utf-8")
    cases = (
        success("package_exact_golden", "package_id", b"source-a", b"content://private/tree/secret.nes"),
        success("source_exact_golden", "saf_source_id", b"content://com.android.externalstorage.documents/tree/primary%3ARoms"),
        success("variant_exact_golden", "variant_id", package_id, b"RAW", payload_hash_lower.upper()),
        success("game_lowercase_hash", "provisional_game_id", payload_hash_lower),
        success("entry_exact_golden", "entry_outcome_id", package_id, b"41422E4E4553@42"),
        success("framing_ab_c", "package_id", b"ab", b"c"),
        success("framing_a_bc", "package_id", b"a", b"bc"),
        success("chinese", "package_id", "来源".encode(), "魂斗罗.nes".encode()),
        success("emoji", "saf_source_id", "folder/🎮".encode()),
        success("nfc_distinct", "entry_outcome_id", "pkg:café".encode(), b"RAW"),
        success("nfd_distinct", "entry_outcome_id", "pkg:cafe\u0301".encode(), b"RAW"),
        success("embedded_nul", "package_id", b"source\x00id", b"document\x00key"),
        success("edge_spaces_preserved", "entry_outcome_id", b" pkg:value ", b" RAW "),
        success("nonblank_mongolian_vowel_separator", "saf_source_id", "\u180e".encode()),
        success("nonblank_next_line", "saf_source_id", "\u0085".encode()),
        success("nonblank_zero_width_space", "saf_source_id", "\u200b".encode()),
        success("nonblank_bom", "saf_source_id", "\ufeff".encode()),
        success("variant_lowercase_hash", "variant_id", b"pkg:value", b"RAW", b"abcdef0123456789" * 4),
        failure("blank_empty", "package_id", "ID input must not be blank", b"", b"document"),
        failure("blank_ascii_controls", "package_id", "ID input must not be blank", b"\x09\x0a\x0b\x0c\x0d", b"document"),
        failure("blank_file_separators", "saf_source_id", "ID input must not be blank", "\u001c\u001d\u001e\u001f ".encode()),
        failure("blank_no_break_space", "entry_outcome_id", "ID input must not be blank", b"pkg:value", "\u00a0".encode()),
        failure("blank_ogham", "package_id", "ID input must not be blank", "\u1680".encode(), b"document"),
        failure("blank_en_quad_through_hair", "package_id", "ID input must not be blank", "\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200a".encode(), b"document"),
        failure("blank_line_paragraph", "saf_source_id", "ID input must not be blank", "\u2028\u2029".encode()),
        failure("blank_narrow_no_break", "entry_outcome_id", "ID input must not be blank", b"pkg:value", "\u202f".encode()),
        failure("blank_medium_math", "package_id", "ID input must not be blank", "\u205f".encode(), b"document"),
        failure("blank_ideographic", "package_id", "ID input must not be blank", "\u3000".encode(), b"document"),
        failure("fullwidth_hex_rejected", "provisional_game_id", "payload SHA-256 must be hexadecimal", fullwidth_hash),
        failure("arabic_hex_rejected", "provisional_game_id", "payload SHA-256 must be hexadecimal", arabic_hash),
        failure("emoji_hex_rejected", "provisional_game_id", "payload SHA-256 must be hexadecimal", ("😀" * 32).encode()),
        failure("short_hash_rejected", "provisional_game_id", "payload SHA-256 must be 64 hex characters", b"ABCDEF"),
        failure("variant_bad_hash_precedes_blank", "variant_id", "payload SHA-256 must be hexadecimal", b"", b"RAW", b"Z" * 64),
        failure("invalid_utf8_overlong", "package_id", "ID input must be valid Unicode", b"\xc0\xaf", b"document"),
        failure("invalid_utf8_truncated", "saf_source_id", "ID input must be valid Unicode", b"\xe2\x82"),
        failure("invalid_utf8_surrogate", "entry_outcome_id", "ID input must be valid Unicode", b"pkg:value", b"\xed\xa0\x80"),
        failure("invalid_utf8_too_large", "package_id", "ID input must be valid Unicode", b"\xf4\x90\x80\x80", b"document"),
        failure("invalid_utf8_continuation", "package_id", "ID input must be valid Unicode", b"\x80", b"document"),
    )

    expected_goldens = {
        "package_exact_golden": "pkg:99BDAC5F521F764F18A3D37CF0A700AE7FCBC43F008677B49089BAD0A5AA3C37",
        "source_exact_golden": "source:F63252A7AC3F4F62ABFC5CD6CF96F7B863AF14E61AA6A5E1C40DC804B9318BE4",
        "variant_exact_golden": "variant:DA056F49718D6A203065D0E9E9AADDB85BE7FB3BA11E677315A7BAB6D2154E07",
        "game_lowercase_hash": "game:239F59ED55E737C77147CF55AD0C1B030B6D7EE748A7426952F9B852D5A935E5",
        "entry_exact_golden": "entry:BFFB25447E25F53B3AA2C799C06B02249A05EAAA41CC63A5504572BBB470B052",
    }
    actual_goldens = {case.case_id: case.expected_value for case in cases if case.case_id in expected_goldens}
    if actual_goldens != expected_goldens:
        raise AssertionError(f"stable ID oracle disagrees with frozen goldens: {actual_goldens}")
    if next(case for case in cases if case.case_id == "framing_ab_c").expected_value == next(
        case for case in cases if case.case_id == "framing_a_bc"
    ).expected_value:
        raise AssertionError("length framing must distinguish concatenation boundaries")
    if next(case for case in cases if case.case_id == "nfc_distinct").expected_value == next(
        case for case in cases if case.case_id == "nfd_distinct"
    ).expected_value:
        raise AssertionError("NFC and NFD inputs must remain distinct")
    return cases


def expected_corpus() -> dict[str, bytes]:
    contents: dict[str, bytes] = {}
    hash_rows = ["\t".join(HASH_HEADER)]
    for case_id, blob in hash_blobs():
        blob_name = case_id + ".bin"
        if blob_name in contents:
            raise AssertionError(f"duplicate generated filename: {blob_name}")
        contents[blob_name] = blob
        hash_rows.append("\t".join((
            SCHEMA_VERSION,
            case_id,
            blob_name,
            str(len(blob)),
            hashlib.sha256(blob).hexdigest(),
            hashlib.sha1(blob).hexdigest().upper(),
            hashlib.sha256(blob).hexdigest().upper(),
            f"{binascii.crc32(blob) & 0xFFFFFFFF:08X}",
        )))
    stable_rows = ["\t".join(STABLE_HEADER)]
    for case in stable_cases():
        arity = OPERATIONS[case.operation]
        if len(case.args) != arity:
            raise AssertionError(f"{case.case_id}: operation arity mismatch")
        encoded_args = [argument.hex() for argument in case.args]
        encoded_args.extend(["NONE"] * (3 - len(encoded_args)))
        stable_rows.append("\t".join((
            SCHEMA_VERSION,
            case.case_id,
            case.operation,
            *encoded_args,
            case.outcome,
            case.expected_value,
            case.expected_message,
        )))
    contents["hashes.tsv"] = ("\n".join(hash_rows) + "\n").encode("ascii")
    contents["stable_ids.tsv"] = ("\n".join(stable_rows) + "\n").encode("ascii")
    validate_expected_corpus(contents)
    return contents


def is_identifier(value: str) -> bool:
    return re.fullmatch(r"[a-z][a-z0-9_]*", value) is not None


def is_lower_hex(value: str, length: int | None = None, allow_empty: bool = False) -> bool:
    if length is not None and len(value) != length:
        return False
    if not value:
        return allow_empty
    return len(value) % 2 == 0 and re.fullmatch(r"[0-9a-f]+", value) is not None


def validate_expected_corpus(contents: dict[str, bytes]) -> None:
    expected_names = {"hashes.tsv", "stable_ids.tsv"}
    expected_names.update(case_id + ".bin" for case_id, _ in hash_blobs())
    if set(contents) != expected_names:
        raise AssertionError("generated corpus filenames are incomplete or unexpected")
    if any("/" in name or "\\" in name or name in {".", ".."} for name in contents):
        raise AssertionError("generated corpus contains path traversal")

    hash_lines = contents["hashes.tsv"].decode("ascii").splitlines()
    stable_lines = contents["stable_ids.tsv"].decode("ascii").splitlines()
    if tuple(hash_lines[0].split("\t")) != HASH_HEADER or tuple(stable_lines[0].split("\t")) != STABLE_HEADER:
        raise AssertionError("generated manifest header mismatch")
    if len(hash_lines) != 1 + len(hash_blobs()) or len(stable_lines) != 1 + len(stable_cases()):
        raise AssertionError("generated manifest case count mismatch")
    if not contents["hashes.tsv"].endswith(b"\n") or not contents["stable_ids.tsv"].endswith(b"\n"):
        raise AssertionError("generated manifests must end in LF")
    if b"\r" in contents["hashes.tsv"] or b"\r" in contents["stable_ids.tsv"]:
        raise AssertionError("generated manifests must use LF")

    hash_ids: set[str] = set()
    blobs: set[str] = set()
    for line in hash_lines[1:]:
        fields = line.split("\t")
        if len(fields) != len(HASH_HEADER):
            raise AssertionError("generated hash row schema mismatch")
        version, case_id, blob, size, blob_sha, sha1, sha256, crc32 = fields
        if version != SCHEMA_VERSION or not is_identifier(case_id) or case_id in hash_ids:
            raise AssertionError("generated hash case set is not canonical")
        if blob != case_id + ".bin" or blob in blobs:
            raise AssertionError("generated blob set is not canonical")
        if size != str(len(contents[blob])):
            raise AssertionError("generated blob size is not canonical")
        if not is_lower_hex(blob_sha, 64) or re.fullmatch(r"[0-9A-F]{40}", sha1) is None:
            raise AssertionError("generated hash encoding is not canonical")
        if re.fullmatch(r"[0-9A-F]{64}", sha256) is None or re.fullmatch(r"[0-9A-F]{8}", crc32) is None:
            raise AssertionError("generated hash encoding is not canonical")
        hash_ids.add(case_id)
        blobs.add(blob)

    stable_ids: set[str] = set()
    for line in stable_lines[1:]:
        fields = line.split("\t")
        if len(fields) != len(STABLE_HEADER):
            raise AssertionError("generated stable-ID row schema mismatch")
        version, case_id, operation, arg0, arg1, arg2, outcome, value, message = fields
        if version != SCHEMA_VERSION or not is_identifier(case_id) or case_id in stable_ids:
            raise AssertionError("generated stable-ID case set is not canonical")
        if operation not in OPERATIONS:
            raise AssertionError("generated stable-ID operation is invalid")
        args = (arg0, arg1, arg2)
        arity = OPERATIONS[operation]
        for index, argument in enumerate(args):
            if index < arity:
                if argument == "NONE" or not is_lower_hex(argument, allow_empty=True):
                    raise AssertionError("generated stable-ID argument is not canonical hex")
            elif argument != "NONE":
                raise AssertionError("generated stable-ID unused argument must be NONE")
        if outcome == "SUCCESS":
            if message != "NONE" or value == "NONE":
                raise AssertionError("generated SUCCESS row is incoherent")
        elif outcome == "ERROR":
            if value != "NONE" or message in {"", "NONE"}:
                raise AssertionError("generated ERROR row is incoherent")
        else:
            raise AssertionError("generated stable-ID outcome is invalid")
        stable_ids.add(case_id)


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
        print(f"refusing to generate: remove reparse/symlink output path {OUTPUT_DIR}", file=sys.stderr)
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
    print(f"generated {len(expected_files)} identity fixture files in {OUTPUT_DIR}")
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
        print(f"identity fixture corpus is current ({len(expected_files)} files)")
        return 0
    return generate(expected_files)


if __name__ == "__main__":
    sys.exit(main())
