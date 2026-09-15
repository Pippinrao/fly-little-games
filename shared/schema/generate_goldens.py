#!/usr/bin/env python3
"""Emit session wire goldens (legal + negatives) into the four schema trees."""

from __future__ import annotations

import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CANONICAL = Path(__file__).resolve().parent / "golden"
COPIES = (
    CANONICAL,
    ROOT / "app" / "src" / "test" / "resources" / "flynes_session_v1" / "golden",
    ROOT / "ios" / "abi" / "flynes_session_v1" / "golden",
    ROOT / "harmony" / "schema" / "flynes_session_v1" / "golden",
)

P256_G = bytes.fromhex(
    "04"
    "6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
    "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5"
)

P256_2G = bytes.fromhex(
    "04"
    "7cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978"
    "07775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1"
)


def u16(n: int) -> bytes:
    return int(n).to_bytes(2, "big")


def u32(n: int) -> bytes:
    return int(n).to_bytes(4, "big")


def u64(n: int) -> bytes:
    return int(n).to_bytes(8, "big")


def z(n: int) -> bytes:
    return bytes(n)


def fill(n: int, value: int = 0xA5) -> bytes:
    return bytes([value & 0xFF]) * n


def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def domain_hash(domain: str, payload: bytes) -> bytes:
    return sha256(domain.encode("ascii") + u32(len(payload)) + payload)


def hex_of(digest: bytes) -> str:
    return digest.hex()


def frame_cursor(kind: int = 0, index: int = 0) -> bytes:
    return bytes([kind]) + z(7) + u64(index)


def identity_ref() -> bytes:
    key_id = sha256(b"flynes-identity-key-id-v1" + u32(65) + P256_G)
    return u16(1) + z(6) + key_id + P256_G + z(7)


def write_case(root: Path, name: str, legal: bytes, domain: str, enum_off: int | None,
               reserved_off: int | None, extra: dict | None = None) -> dict:
    case_dir = root / name
    case_dir.mkdir(parents=True, exist_ok=True)
    if domain == "prefixed03-flynes-tail-component-v1":
        digest = sha256(bytes([0x03]) + b"flynes-tail-component-v1" + legal)
        hash_hex = hex_of(digest)
    elif domain == "prefixed10-flynes-input-bundle-v1":
        digest = sha256(bytes([0x10]) + b"flynes-input-bundle-v1" + u32(len(legal)) + legal)
        hash_hex = hex_of(digest)
    elif domain:
        hash_hex = hex_of(domain_hash(domain, legal))
    else:
        hash_hex = hex_of(sha256(legal))

    (case_dir / "legal.bin").write_bytes(legal)
    (case_dir / "legal.hash").write_text(hash_hex + "\n", encoding="ascii")
    (case_dir / "truncate.bin").write_bytes(legal[:-1] if legal else b"")
    (case_dir / "trailing.bin").write_bytes(legal + b"\x00")
    unknown = bytearray(legal)
    if enum_off is not None and enum_off < len(unknown):
        unknown[enum_off] = 0xFF
    (case_dir / "unknown_enum.bin").write_bytes(bytes(unknown))
    reserved = bytearray(legal)
    if reserved_off is not None and reserved_off < len(reserved):
        reserved[reserved_off] = 0x01
    (case_dir / "nonzero_reserved.bin").write_bytes(bytes(reserved))
    record = {
        "domain": domain,
        "enum_offset": enum_off,
        "length": len(legal),
        "name": name,
        "reserved_offset": reserved_off,
        "sha256": hash_hex,
    }
    if extra:
        record.update(extra)
    type_name = extra.get("kind") if extra else None
    if extra and extra.get("message"):
        type_name = extra["message"]
    (case_dir / "type.txt").write_text((type_name or name) + "\n", encoding="ascii")
    if extra and extra.get("negative_only"):
        (case_dir / "negative_only.txt").write_text("1\n", encoding="ascii")
    if enum_off is None:
        (case_dir / "skip_enum.txt").write_text("1\n", encoding="ascii")
    if reserved_off is None:
        (case_dir / "skip_reserved.txt").write_text("1\n", encoding="ascii")
    return record


def build_records() -> list[dict]:
    sid = fill(16, 0x11)
    bid = fill(16, 0x22)
    lid = fill(16, 0x33)
    key = fill(32, 0x44)
    h32 = fill(32, 0x55)
    nonce = fill(32, 0x66)
    sig = fill(32, 0x77) + fill(32, 0x88)
    cursor0 = frame_cursor(0, 0)

    records: list[tuple] = []

    legal = identity_ref()
    records.append(("identity_verifier_ref_v1", legal, "flynes-identity-verifier-ref-v1", None, 2, {"kind": "0x0209"}))

    legal = u16(1) + z(6) + sid + bytes([0]) + z(7) + key + u64(1) + u64(0) + u64(0) + z(32)
    records.append(("input_sequence_ledger_v1", legal, "flynes-input-sequence-ledger-v1", None, 2, {"kind": "0x0202"}))

    legal = (u16(1) + z(6) + sid + bytes([0, 1]) + z(6) + u64(1) + u64(1) + key + u64(1)
             + u64(1) + u64(64) + u64(0) + nonce)
    records.append(("input_sequence_reservation_v1", legal, "flynes-input-sequence-reservation-v1", 25, 2, {"kind": "0x0203"}))

    legal = (u16(1) + z(6) + sid + bid + u64(1) + u64(0) + u64(0) + u64(1) + u64(30000000001)
             + h32 + h32)
    records.append(("reconnect_deadline_record_v1", legal, "flynes-reconnect-deadline-record-v1", None, 2, {"kind": "0x0205"}))

    legal = (u16(1) + z(6) + sid + lid + bid + u64(1) + u64(1) + u64(0) + bytes([1, 1, 0])
             + z(5) + z(32) + cursor0 + h32 + h32 + h32)
    records.append(("dual_run_fence_v1", legal, "flynes-dual-run-fence-v1", 81, 2, {"kind": "0x0201"}))

    legal = (u16(1) + z(6) + sid + bytes([0, 1]) + z(6) + u64(1) + u64(1) + key + u32(64)
             + z(4) + nonce + h32 + z(32))
    records.append(("input_reservation_request_v1", legal, "flynes-input-sequence-reserve-req-v1", 25, 2, {"kind": "0x020d"}))

    reservation = (u16(1) + z(6) + sid + bytes([0, 1]) + z(6) + u64(1) + u64(1) + key + u64(1)
                   + u64(1) + u64(64) + u64(0) + nonce)
    res_hash = domain_hash("flynes-input-sequence-reservation-v1", reservation)
    legal = u16(1) + z(6) + h32 + h32 + h32 + reservation + res_hash + h32
    records.append(("input_reservation_grant_v1", legal, "flynes-input-sequence-reserve-grant-v1", None, 2, {"kind": "0x020a"}))

    legal = u16(1) + z(6) + sid + bytes([0]) + z(7) + key + h32 + h32 + h32 + h32
    records.append(("input_reservation_ack_v1", legal, "flynes-input-sequence-reserve-ack-v1", None, 2, {"kind": "0x020b"}))

    legal = u16(1) + z(6) + sid + bid + u64(1) + bytes([0]) + z(7) + key + h32 + h32 + h32 + h32 + h32
    records.append(("input_reservation_finalized_v1", legal, "flynes-input-sequence-reserve-finalized-v1", None, 2, {"kind": "0x020c"}))

    legal = (u16(1) + z(6) + bytes([1, 1, 0, 2, 0]) + z(3) + fill(16, 0x01) + sid + bid
             + u64(1) + h32 + z(32) + h32 + cursor0 + u64(1) + u64(1) + key + u32(2) + u32(0))
    records.append(("prime_authorization_v1", legal, "flynes-prime-authorization-v1", 8, 2, {"kind": "0x020e"}))

    body168 = (u16(1) + z(6) + fill(16, 0x02) + sid + bid + u64(1) + bytes([0, 1]) + z(6)
               + u64(1) + u64(1) + h32 + h32 + u64(64) + u32(1) + z(4))
    legal = body168 + sig + sig
    records.append(("epoch_input_close_certificate_v1", legal, "flynes-epoch-input-close-certificate-v1", 65, 2, {"kind": "0x020f"}))

    pretag176 = (u16(1) + z(6) + sid + bid + u64(1) + u64(1) + u64(1) + bytes([0]) + z(7)
                 + key + u64(1) + u32(1) + u32(0) + u64(1) + h32 + fill(16, 0x09))
    legal = pretag176 + sig
    records.append(("suspend_intent_v1", legal, "flynes-suspend-intent-v1", None, 2, {"kind": "0x0210"}))

    pretag272 = (u16(1) + z(6) + bytes([1, 0, 0]) + z(5) + fill(16, 0x03) + sid + bid
                 + u64(1) + u64(1) + u64(1) + key + z(32) + z(32) + u32(1) + u32(1)
                 + u64(1) + u64(1) + h32 + z(32))
    legal = pretag272 + sig
    records.append(("input_range_authorization_v1", legal, "flynes-input-range-authorization-v1", 8, 2, {"kind": "0x0211"}))

    pretag248 = (u16(1) + z(6) + h32 + sid + bytes([1]) + z(7) + identity_ref() + P256_2G + z(7))
    legal = pretag248 + fill(32, 0x77) + fill(32, 0x44)
    records.append(("session_signing_key_binding_v1", legal, "flynes-session-signing-key-binding-hash-v1", 56, 2, {"kind": "0x0212"}))

    preimage = (u16(1) + z(6) + bytes([1]) + z(7) + z(32) + h32 + fill(32, 0x56)
                + fill(320, 0x10) + fill(320, 0x20))
    legal = preimage + sig + sig
    records.append(("pair_transcript_v1", legal, "flynes-pair-transcript-object-v1", 8, 2, {"kind": "0x0213"}))

    legal = (u16(1) + z(6) + sid + bid + u64(1) + h32 + z(32) + h32 + z(32)
             + bytes([1, 1]) + z(14))
    records.append(("opaque_recovery_failure_evidence_v1", legal, "flynes-opaque-recovery-failure-evidence-v1", 176, 2, {"kind": "0x0208"}))

    legal = (u16(1) + z(6) + sid + bid + u64(1) + h32 + h32 + z(32) + h32 + u64(1)
             + h32 + z(16) + z(32) + bytes([1, 1, 1, 0, 0]) + z(3))
    records.append(("preprepare_terminal_evidence_v1", legal, "flynes-preprepare-terminal-evidence-v1", 264, 2, {"kind": "0x0206"}))

    legal = (u16(1) + z(6) + sid + bid + u64(1) + h32 + u64(1) + fill(16, 0x0A)
             + fill(16, 0x0B) + h32 + h32 + z(32) + h32 + h32 + z(16) + z(32)
             + bytes([1, 0, 1, 0, 1, 0, 0, 0, 0]) + z(7))
    records.append(("terminal_recovery_evidence_v1", legal, "flynes-terminal-recovery-evidence-v1", 328, 2, {"kind": "0x0207"}))

    legal = (u16(1) + z(6) + fill(16, 0x0C) + bytes([1]) + z(7) + h32 + sid + bid
             + u64(1) + u64(1) + cursor0 + u64(1) + h32 + h32 + u64(1))
    records.append(("save_commit_decision_v1", legal, "flynes-save-commit-decision-v1", 24, 2, {"kind": "0x0304"}))

    body328 = (u16(1) + z(6) + fill(16, 0x0D) + h32 + sid + lid + bid + u64(1) + u64(1)
               + key + u64(1) + u64(1) + u64(2) + u64(1) + u64(1) + h32 + z(32) + z(32)
               + z(32) + bytes([1, 2]) + u16(1) + z(4))
    legal = body328 + sig
    records.append(("end_commit_certificate_v1", legal, "flynes-end-commit-certificate-hash-v1", None, 2, {"kind": "0x0308"}))

    legal = (u16(1) + z(6) + fill(16, 0x0D) + h32 + h32 + sid + lid + bid + u64(1) + u64(1)
             + key + u64(1) + u64(1) + u64(2) + u64(1) + u64(1) + h32 + z(32) + z(32)
             + z(32) + bytes([1, 2]) + u16(1) + z(4))
    records.append(("end_commit_decision_v1", legal, "flynes-end-commit-decision-v1", 352, 2, {"kind": "0x0307"}))

    body328s = (u16(1) + z(6) + sid + lid + bid + u64(1) + u64(1) + cursor0 + u64(1)
                + h32 + h32 + h32 + u64(1) + h32 + h32 + u64(1) + u64(1) + fill(16, 0x0E)
                + key)
    legal = body328s + sig
    records.append(("save_commit_certificate_v1", legal, "flynes-save-commit-certificate-hash-v1", None, 2, {"kind": "0x0303"}))

    body336 = (u16(1) + z(6) + fill(16, 0x0E) + h32 + h32 + h32 + sid + lid + bid
               + u64(1) + u64(1) + cursor0 + u64(1) + u64(1) + u64(1) + h32 + key
               + u64(1) + bytes([1]) + z(7) + h32)
    legal = body336 + sig
    records.append(("save_publication_certificate_v1", legal, "flynes-save-publication-certificate-hash-v1", 296, 2, {"kind": "0x0305"}))

    body248 = (u16(1) + z(6) + fill(16, 0x0F) + h32 + h32 + lid + bid + key + key
               + u64(1) + u64(2) + u64(1) + u64(1) + sid + u64(1) + u64(1))
    legal = body248 + sig
    records.append(("offline_release_certificate_v1", legal, "flynes-offline-release-certificate-hash-v1", None, 2, {"kind": "0x0301"}))

    legal = u16(1) + z(6) + h32 + h32 + h32 + identity_ref() + u32(0)
    records.append(("end_closure_manifest_v1", legal, "flynes-end-closure-manifest-v1", None, 2, {"kind": "0x010f"}))

    entries32 = b"".join(u16(1) + u16(0) + fill(32, (i + 1) & 0xFF) for i in range(32))
    legal = u16(1) + z(6) + h32 + h32 + h32 + identity_ref() + u32(32) + entries32
    records.append(("end_closure_manifest_v1_n32", legal, "flynes-end-closure-manifest-v1", None, 2, {"kind": "0x010f"}))

    legal = u16(1) + z(6) + sid + lid + bid + u64(1) + u64(0) + u32(0) + z(4)
    records.append(("published_save_index_v1", legal, "flynes-published-save-index-v1", None, 2, {"kind": "0x0110"}))

    save_entries = b"".join(u64(i + 1) + fill(32, ((i + 1) * 3) & 0xFF) for i in range(1024))
    legal = u16(1) + z(6) + sid + lid + bid + u64(1) + u64(1024) + u32(1024) + z(4) + save_entries
    records.append(("published_save_index_v1_n1024", legal, "flynes-published-save-index-v1", None, 2, {"kind": "0x0110"}))

    legal = (u16(1) + z(6) + sid + bid + u64(1) + bytes([0]) + z(7) + u64(1) + h32
             + u32(0) + z(4))
    records.append(("open_input_reservation_set_v1", legal, "flynes-open-input-reservation-set-v1", None, 2, {"kind": "0x0112"}))

    open_entries = b"".join(
        u64(i * 8 + 1) + u64(i * 8 + 8) + bytes([3, 1]) + z(6) + h32 + h32 + h32 + h32 + h32
        for i in range(8)
    )
    legal = (u16(1) + z(6) + sid + bid + u64(1) + bytes([0]) + z(7) + u64(1) + h32
             + u32(8) + z(4) + open_entries)
    records.append(("open_input_reservation_set_v1_n8", legal, "flynes-open-input-reservation-set-v1", None, 2, {"kind": "0x0112"}))

    legal = bytes(256 * 240 * 4)
    records.append(("transition_rgba8888_frame_v1", legal, "", None, None, {"kind": "0x000b"}))

    legal = frame_cursor(0, 0)
    records.append(("frame_cursor_genesis", legal, "", 0, 1, {"message": "FrameCursorV1"}))
    legal = frame_cursor(1, 0)
    records.append(("frame_cursor_frame0", legal, "", 0, 1, {"message": "FrameCursorV1"}))

    legal = bytes([0]) + z(7)
    records.append(("evidence_cursor_none", legal, "", 0, 1, {"message": "EvidenceCursorV1"}))
    legal = bytes([1]) + z(7) + u64(1) + frame_cursor(1, 0) + h32
    records.append(("evidence_cursor_present", legal, "", 0, 1, {"message": "EvidenceCursorV1"}))

    port_real = bytes([1, 0]) + u16(0) + u32(0x30) + u64(1)  # UP+DOWN, normalized later
    # encode already-normalized buttons=0 for the committed legal bytes
    port_real = bytes([1, 0]) + u16(0) + u32(0) + u64(1)
    port_clear = bytes([3, 2]) + u16(0) + u32(0) + u64(1)
    port_neutral = bytes([4, 0]) + u16(0) + u32(0) + u64(0)
    legal = (u16(1) + sid + bid + u64(1) + u64(1) + u64(1) + u64(1) + u64(0) + u64(1)
             + bytes([0]) + z(7) + port_real + port_clear + port_neutral + port_neutral)
    records.append(("canonical_input_bundle_v1", legal, "prefixed10-flynes-input-bundle-v1", 90, 83, {"message": "CanonicalInputBundleV1"}))

    pair_hash = fill(32, 0xAB)
    bind_nonce = sha256(b"flynes-initial-bind-v1" + pair_hash)[:16]
    legal = u16(1) + z(6) + bytes([1]) + z(7) + pair_hash + sid + z(32) + u64(0) + fill(16, 0xCD) + bind_nonce
    records.append(("channel_bind_v1_initial", legal, "", 8, 2, {"message": "ChannelBindV1"}))

    legal = (u16(1) + z(6) + sid + bid + u64(1) + u64(1) + fill(16, 0x0A) + fill(16, 0x0B)
             + bytes([1, 2, 0]) + z(5) + h32 + z(16) + z(32))
    records.append(("channel_resume_summary_v1", legal, "flynes-channel-resume-summary-v1", 88, 2, {"message": "ChannelResumeSummaryV1"}))

    # EndPackage unknown-critical-tag: header + tag 99
    tlv = u16(1) + u16(1) + u16(99) + u32(1) + bytes([0])
    records.append(("end_package_unknown_critical", tlv, "flynes-end-package-v1", None, None, {"kind": "0x0306", "negative_only": True}))

    return records


def main() -> int:
    records = build_records()
    # recreate canonical golden tree
    if CANONICAL.exists():
        shutil.rmtree(CANONICAL)
    CANONICAL.mkdir(parents=True)
    manifest = []
    for name, legal, domain, enum_off, reserved_off, extra in records:
        manifest.append(write_case(CANONICAL, name, legal, domain, enum_off, reserved_off, extra))
    (CANONICAL / "manifest.json").write_bytes(
        (json.dumps({"cases": manifest}, indent=2, sort_keys=True) + "\n").encode("utf-8").replace(b"\r\n", b"\n")
    )
    payload = CANONICAL
    for dest in COPIES[1:]:
        if dest.exists():
            shutil.rmtree(dest)
        shutil.copytree(payload, dest)
    print(f"wrote {len(manifest)} golden cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
