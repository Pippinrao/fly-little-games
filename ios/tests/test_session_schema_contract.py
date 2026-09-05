#!/usr/bin/env python3
"""Host checks for the iOS session schema copy. Runnable on Windows."""

from pathlib import Path
import hashlib
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
CANONICAL = ROOT / "shared" / "schema"
IOS_COPY = ROOT / "ios" / "abi" / "flynes_session_v1"
DOMAIN = b"flynes-session-schema-registry-v1"
STAGE1 = (
    "ios/CMakeLists.txt",
    "ios/src/main.mm",
    "ios/src/portability_smoke.cpp",
    "ios/src/portability_smoke.hpp",
)
FORBIDDEN = re.compile(
    r"flynes_session|fly_session_",
    flags=re.IGNORECASE,
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def registry_hash(schema: bytes) -> str:
    length = len(schema)
    return hashlib.sha256(DOMAIN + length.to_bytes(4, "big") + schema).hexdigest()


def main() -> int:
    schema = (CANONICAL / "flynes_session_v1.schema").read_bytes()
    published = (CANONICAL / "schema_registry_hash.txt").read_text(encoding="ascii").strip()
    ios_schema = (IOS_COPY / "flynes_session_v1.schema").read_bytes()
    ios_hash = (IOS_COPY / "schema_registry_hash.txt").read_bytes()
    canonical_hash = (CANONICAL / "schema_registry_hash.txt").read_bytes()

    require(b"\r" not in schema, "canonical schema must be LF-normalized")
    require(ios_schema == schema, "iOS schema copy must be byte-identical")
    require(ios_hash == canonical_hash, "iOS hash copy must be byte-identical")
    require(registry_hash(schema) == published, "published hash must recompute")
    require("0x0000" in schema.decode("utf-8"), "schema must mark 0x0000 illegal")
    require("0x0308" in schema.decode("utf-8"), "schema must include END_COMMIT_CERTIFICATE_V1")

    symbols = (ROOT / "ios" / "abi" / "flynes_app.symbols.txt").read_text(encoding="utf-8")
    require("fly_session_" not in symbols, "Stage-1 symbols must not export session")

    for relative in STAGE1:
        text = (ROOT / relative).read_text(encoding="utf-8")
        require(not FORBIDDEN.search(text),
                f"Stage-1 file {relative} must not mention session")

    print("flynes_ios_session_schema_contract: PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except AssertionError as error:
        print(f"flynes_ios_session_schema_contract: FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
