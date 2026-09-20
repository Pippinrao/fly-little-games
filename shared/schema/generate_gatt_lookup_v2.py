#!/usr/bin/env python3
"""Emit the authoritative GATT lookup V2 registry and exact positive goldens."""

from __future__ import annotations

import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CANONICAL = Path(__file__).resolve().parent / "gatt_lookup_v2"
COPIES = (
    CANONICAL,
    ROOT / "app" / "src" / "test" / "resources" / "gatt_lookup_v2",
    ROOT / "ios" / "abi" / "gatt_lookup_v2",
    ROOT / "harmony" / "schema" / "gatt_lookup_v2",
)


def domain_hash(domain: bytes, payload: bytes) -> bytes:
    return hashlib.sha256(domain + len(payload).to_bytes(4, "big") + payload).digest()


def logical(message_type: int, body: bytes) -> bytes:
    header = bytes((2, message_type, 0, 0)) + len(body).to_bytes(4, "big")
    prefix = header + body
    return prefix + domain_hash(b"flynes-gatt-logical-v2", prefix)


def pair_context() -> bytes:
    value = bytearray(80)
    value[1] = 1
    value[9] = 2
    value[12] = 1
    value[16:32] = bytes(range(0x10, 0x20))
    value[32:48] = bytes(range(0x30, 0x40))
    value[48:52] = (60000).to_bytes(4, "big")
    value[64:80] = bytes(range(0x70, 0x80))
    return bytes(value)


def schema() -> dict:
    return {
        "schema_id": "flynes_gatt_lookup_v2",
        "outer_version": 2,
        "wire_endian": "network",
        "maximum_logical_bytes": 4096,
        "hash_domain": "flynes-gatt-logical-v2",
        "legacy_v1_types": list(range(1, 27)),
        "rejected_types": [0, 29],
        "messages": [
            {
                "type": 27,
                "name": "CODE_LOOKUP_REQUEST",
                "direction": "RESPONDER_TO_INITIATOR",
                "body_length": 32,
                "logical_length": 72,
                "fields": [
                    ["version", 0, 2, "u16be=2"],
                    ["reserved_zero", 2, 6, "zero"],
                    ["code", 8, 6, "ASCII_DIGITS"],
                    ["reserved_zero_2", 14, 2, "zero"],
                    ["request_nonce", 16, 16, "nonzero"],
                ],
            },
            {
                "type": 28,
                "name": "CODE_LOOKUP_MATCH",
                "direction": "INITIATOR_TO_RESPONDER",
                "body_length": 136,
                "logical_length": 176,
                "fields": [
                    ["version", 0, 2, "u16be=2"],
                    ["reserved_zero", 2, 6, "zero"],
                    ["echo_request_nonce", 8, 16, "nonzero"],
                    ["pair_context_v1", 24, 80, "wire_major=2,route=BLE_ANONYMOUS"],
                    ["pair_context_hash", 104, 32, "flynes-pair-context-v1"],
                ],
            },
        ],
    }


def main() -> int:
    schema_bytes = (json.dumps(schema(), indent=2, sort_keys=True) + "\n").encode("ascii")
    nonce = bytes(range(0xA0, 0xB0))
    request_body = (2).to_bytes(2, "big") + bytes(6) + b"012345" + bytes(2) + nonce
    context = pair_context()
    match_body = ((2).to_bytes(2, "big") + bytes(6) + nonce + context +
                  domain_hash(b"flynes-pair-context-v1", context))
    request = logical(27, request_body)
    match = logical(28, match_body)
    manifest = {
        "request": {"file": "code_lookup_request.bin", "bytes": 72,
                    "sha256": hashlib.sha256(request).hexdigest()},
        "match": {"file": "code_lookup_match.bin", "bytes": 176,
                  "sha256": hashlib.sha256(match).hexdigest()},
    }
    for destination in COPIES:
        if destination.exists():
            shutil.rmtree(destination)
        destination.mkdir(parents=True)
        (destination / "gatt_lookup_v2.schema").write_bytes(schema_bytes)
        (destination / "registry_hash.txt").write_text(
            domain_hash(b"flynes-gatt-lookup-registry-v2", schema_bytes).hex() + "\n",
            encoding="ascii", newline="\n")
        (destination / "manifest.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
            encoding="ascii", newline="\n")
        (destination / "code_lookup_request.bin").write_bytes(request)
        (destination / "code_lookup_match.bin").write_bytes(match)
    print("wrote authoritative GATT lookup V2 schema and 2 goldens")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
