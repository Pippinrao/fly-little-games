#!/usr/bin/env python3
"""Emit the normative flynes_session_v1 schema and registry hash (LF bytes)."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CANONICAL = Path(__file__).resolve().parent
COPIES = (
    CANONICAL,
    ROOT / "app" / "src" / "test" / "resources" / "flynes_session_v1",
    ROOT / "ios" / "abi" / "flynes_session_v1",
    ROOT / "harmony" / "schema" / "flynes_session_v1",
)

REGISTRY_DOMAIN = b"flynes-session-schema-registry-v1"


def field(name, wire, endian="network", cardinality="required_1", note=""):
    item = {
        "cardinality": cardinality,
        "endian": endian,
        "name": name,
        "wire_type": wire,
    }
    if note:
        item["note"] = note
    return item


def child(field_name, kinds, cardinality, note=""):
    item = {
        "cardinality": cardinality,
        "field": field_name,
        "kinds": kinds,
    }
    if note:
        item["note"] = note
    return item


def kind(
    value,
    name,
    max_decode_length,
    closure_role,
    layout,
    hash_domain,
    fields,
    children,
    enums=None,
    reserved_zero=True,
    fixed_length=None,
    notes="",
    hash_preimage="",
):
    item = {
        "children": children,
        "closure_role": closure_role,
        "enums": enums or {},
        "fields": fields,
        "fixed_length": fixed_length,
        "hash_domain": hash_domain,
        "hash_preimage": hash_preimage
        or (
            f'SHA256("{hash_domain}" || u32be(len) || exact_bytes)'
            if hash_domain
            else "spec_did_not_freeze_named_domain; store uses expected_hash of exact bytes"
        ),
        "kind": value,
        "layout": layout,
        "max_decode_length": max_decode_length,
        "name": name,
        "notes": notes,
        "reserved_zero_required": reserved_zero,
        "unknown_enum_rejects": True,
        "unassigned_kind_illegal": True,
        "version_u16be": 1 if layout != "opaque_leaf" else None,
    }
    return item


def build_schema():
    kinds = []

    kinds.append(
        kind(
            "0x0001",
            "RUNTIME_CHECKPOINT_V1",
            8388608,
            "HARD",
            "opaque_leaf",
            "",
            [field("canonical_runtime_bytes", "u8[]", "n/a", "required_1", "uncompressed wrapper over nes_save_state")],
            [],
            notes="HARD leaf. Session-agnostic; no lineage/network/compression fields.",
        )
    )
    kinds.append(
        kind(
            "0x0002",
            "SESSION_CHECKPOINT_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [field("runtime_checkpoint_hash", "bytes32", cardinality="required_1")],
            [child("runtime_checkpoint_hash", ["0x0001"], "exactly_1")],
            notes="HARD manifest envelope binding a runtime checkpoint. Tags not privately assigned.",
        )
    )
    kinds.append(
        kind(
            "0x0003",
            "SRAM_BLOB_V1",
            1048576,
            "HARD",
            "opaque_leaf",
            "",
            [field("canonical_sram_bytes", "u8[]", "n/a")],
            [],
            notes="HARD leaf. Canonical SRAM image, max 1 MiB.",
        )
    )
    kinds.append(
        kind(
            "0x0004",
            "CANONICAL_INPUT_COMPONENT_V1",
            524288,
            "HARD",
            "fixed_header_plus_entries",
            "flynes-tail-component-v1",
            [
                field("entry_count", "u32be"),
                field("entries", "canonical_tail_entry[]", cardinality="0..2048"),
            ],
            [],
            notes="Empty component is exactly u32be(0). Hash uses prefix 0x03 || UTF8(domain) || exact_bytes. Max 512 KiB / 2048 frames.",
            hash_preimage='SHA256(0x03 || UTF8("flynes-tail-component-v1") || exact_component_bytes)',
        )
    )
    kinds.append(
        kind(
            "0x0005",
            "CANONICAL_INPUT_COMPONENT_MANIFEST_V1",
            65536,
            "HARD",
            "fixed_header_plus_entries",
            "flynes-canonical-input-component-manifest-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("total_component_length", "u64be"),
                field("canonical_component_hash", "bytes32"),
                field("chunk_count", "u32be"),
                field("reserved_zero_2", "u8[4]"),
                field("entries", "chunk_entry[1..8]"),
            ],
            [child("chunk_object_hash", ["0x0006"], "1..8")],
            notes="Header 56 bytes; each entry 48 bytes: offset u64be || length u32be || reserved_zero[4] || chunk_object_hash[32]. chunk_count=ceil(total/65536). Empty tail still has one length=4 chunk.",
            fixed_length=None,
        )
    )
    kinds.append(
        kind(
            "0x0006",
            "CANONICAL_INPUT_COMPONENT_CHUNK_V1",
            65536,
            "HARD",
            "opaque_leaf",
            "flynes-canonical-input-component-chunk-v1",
            [field("chunk_bytes", "u8[]", "n/a")],
            [],
            notes="HARD leaf. Parent total still 512 KiB.",
        )
    )
    kinds.append(
        kind(
            "0x0007",
            "SRAM_JOURNAL_COMPONENT_V1",
            1048576,
            "HARD",
            "fixed_header_plus_entries",
            "flynes-sram-journal-component-v1",
            [
                field("entry_count", "u32be"),
                field("entries", "journal_entry[]", note="frame_index u64be || offset u32be || length u32be || bytes[length]"),
            ],
            [],
            notes="Empty journal is u32be(0). Hash uses prefix 0x04 || UTF8(domain) || exact_bytes.",
            hash_preimage='SHA256(0x04 || UTF8("flynes-sram-journal-component-v1") || canonical_journal_component_bytes)',
        )
    )
    kinds.append(
        kind(
            "0x0008",
            "INPUT_RESERVATION_COMPONENT_V1",
            524288,
            "HARD",
            "fixed_header_plus_entries",
            "flynes-input-sequence-reservation-component-v1",
            [
                field("count", "u32be"),
                field("reservations", "reservation_record[]", note="u32be(144)||InputSequenceReservationV1||reservation_hash32"),
            ],
            [child("reservation", ["0x0203"], "0..n")],
            notes="Count=0 is exactly 4 bytes. Prime/terminal/sample require non-empty.",
        )
    )
    kinds.append(
        kind(
            "0x0009",
            "EPOCH_PRIME_COMPONENT_V1",
            524288,
            "HARD",
            "opaque_leaf",
            "",
            [field("prime_bytes", "u8[]", "n/a")],
            [],
            notes="HARD leaf. Expanded HELD_PRIME / PORT_CLEAR_PRIME samples.",
        )
    )
    kinds.append(
        kind(
            "0x000a",
            "TRANSITION_OPERATION_PAYLOAD_V1",
            65536,
            "HARD",
            "opaque_leaf",
            "",
            [field("payload_bytes", "u8[]", "n/a")],
            [],
        )
    )
    kinds.append(
        kind(
            "0x000b",
            "TRANSITION_RGBA8888_FRAME_V1",
            245760,
            "HARD",
            "fixed",
            "",
            [field("rgba8888", "u8[245760]", "n/a", note="256x240x4")],
            [],
            fixed_length=245760,
            notes="HARD leaf. Exact 256*240*4 bytes.",
        )
    )
    kinds.append(
        kind(
            "0x000c",
            "USER_SAVE_PAYLOAD_V1",
            12582912,
            "SAVE_HARD",
            "opaque_leaf",
            "flynes-user-save-payload-v1",
            [field("payload_bytes", "u8[]", "n/a")],
            [],
        )
    )
    kinds.append(
        kind(
            "0x000d",
            "REJECTED_DEADLINE_RAW_V1",
            4096,
            "OPAQUE",
            "opaque_leaf",
            "flynes-rejected-reconnect-deadline-record-v1",
            [field("raw", "u8[0..4096]", "n/a")],
            [],
            notes="0-byte object is present corrupt, not MISSING.",
        )
    )
    kinds.append(
        kind(
            "0x000e",
            "QUARANTINE_RETAINED_BLOB_V1",
            1048576,
            "OPAQUE",
            "opaque_leaf",
            "flynes-quarantine-retained-blob-v1",
            [field("prefix_or_full", "u8[]", "n/a")],
            [],
            notes="Never parsed as claimed kind. OPAQUE/HARD retained leaf.",
        )
    )
    kinds.append(
        kind(
            "0x0101",
            "RECOVERY_POINT_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [
                field("frame_cursor", "FrameCursorV1"),
                field("session_checkpoint_hash", "bytes32"),
                field("sram_revision", "u64be"),
                field("sram_blob_hash", "bytes32"),
            ],
            [
                child("session_checkpoint_hash", ["0x0002"], "exactly_1"),
                child("sram_blob_hash", ["0x0003"], "exactly_1"),
            ],
            notes="HARD manifest. Tags not privately assigned.",
        )
    )
    kinds.append(
        kind(
            "0x0102",
            "AUTHORITY_RECOVERY_HEAD_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [
                field("timeline_epoch", "u64be"),
                field("anchor_recovery_point_hash", "bytes32"),
                field("tail_component_manifest_hash", "bytes32"),
                field("sram_journal_component_hash", "bytes32"),
            ],
            [
                child("anchor_recovery_point_hash", ["0x0101"], "exactly_1"),
                child("tail_component_manifest_hash", ["0x0005"], "exactly_1"),
                child("sram_journal_component_hash", ["0x0007"], "exactly_1"),
            ],
        )
    )
    kinds.append(
        kind(
            "0x0103",
            "REPLAYABLE_PROOF_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [
                field("anchor_recovery_point_hash", "bytes32"),
                field("tail_component_manifest_hash", "bytes32"),
                field("derived_sram_blob_hash", "bytes32"),
            ],
            [
                child("anchor_recovery_point_hash", ["0x0101"], "exactly_1"),
                child("tail_component_manifest_hash", ["0x0005"], "exactly_1"),
                child("derived_sram_blob_hash", ["0x0003"], "exactly_1"),
            ],
        )
    )
    kinds.append(
        kind(
            "0x0104",
            "ACTIVE_SESSION_MANIFEST_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [],
            [
                child("session_config", ["0x0105"], "exactly_1"),
                child("authority_recovery_head", ["0x0102"], "exactly_1"),
                child("open_sets", ["0x0112"], "0..4"),
                child("published_save_index", ["0x0110"], "exactly_1"),
                child("active_suspend_intent_hash", ["0x0210"], "0..4"),
                child("pending_transaction_wal_hash", ["0x0204"], "0..1"),
                child("reconnect_recovery_wal_hash", ["0x0204"], "0..1"),
                child("finalization_repair_wal_hash", ["0x0204"], "0..1"),
                child("released_recovery_tombstone_hash", ["0x0204"], "0..2"),
                child("terminal_coordination_evidence_hash", ["0x0206", "0x0207"], "0..1"),
                child("transition_package", ["0x0107"], "0..2"),
                child("replayable_proof", ["0x0103"], "0..1"),
                child("dual_run_fence", ["0x0201"], "0..1"),
            ],
            notes="HARD manifest. Unknown critical TLV tag rejects. Tags not privately assigned.",
        )
    )
    kinds.append(
        kind(
            "0x0105",
            "SESSION_CONFIG_MANIFEST_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [],
            [
                child("session_signing_key_binding", ["0x0212"], "exactly_2"),
                child("pair_transcript", ["0x0213"], "exactly_1"),
            ],
            notes="Two 0x0212 bindings ordered initiator then responder.",
        )
    )
    kinds.append(
        kind(
            "0x0106",
            "BARRIER_RECOVERY_REF_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [],
            [child("recovery_point", ["0x0101"], "exactly_1")],
        )
    )
    kinds.append(
        kind(
            "0x0107",
            "TRANSITION_PACKAGE_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [field("kind", "u8", note="BARRIER or SWITCH")],
            [],
            notes="Manifest max 64 KiB; uncompressed components total max 12 MiB. Unknown critical tag rejects.",
        )
    )
    kinds.append(
        kind(
            "0x0108",
            "SOURCE_PROGRESS_REF_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [],
            [
                child("offline_progress_point", ["0x0109"], "exactly_1"),
                child("offline_save", ["0x0111"], "0..1"),
                child("ended_save_closure", ["0x010d", "0x0303", "0x0304", "0x0305", "0x0306", "0x0307", "0x0308", "0x010f"], "0..1_set"),
                child("published_save_index", ["0x0110"], "0..1"),
            ],
            notes="GENESIS has no origin child; ACTIVE_OPP none extra; OFFLINE_SAVE exactly 0x0111; ENDED_MULTIPLAYER_SAVE membership vs final-direct exclusive.",
        )
    )
    kinds.append(
        kind(
            "0x0109",
            "OFFLINE_PROGRESS_POINT_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [],
            [
                child("runtime_checkpoint", ["0x0001"], "exactly_1"),
                child("sram_blob", ["0x0003"], "exactly_1"),
            ],
        )
    )
    kinds.append(
        kind(
            "0x010a",
            "SESSION_START_PACKAGE_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [],
            [
                child("source_progress_ref", ["0x0108"], "exactly_1"),
                child("session_config", ["0x0105"], "exactly_1"),
                child("recovery_point", ["0x0101"], "exactly_1"),
                child("dual_run_fence", ["0x0201"], "0..2"),
            ],
        )
    )
    kinds.append(
        kind(
            "0x010b",
            "FORK_HANDOFF_PACKAGE_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [],
            [
                child("offline_progress_point", ["0x0109"], "exactly_1"),
                child("sram_blob", ["0x0003"], "exactly_1"),
                child("offline_save_manifest", ["0x0111"], "exactly_1"),
                child("source", ["0x0102", "0x0103", "0x0208"], "0..1"),
            ],
        )
    )
    kinds.append(
        kind(
            "0x010c",
            "RECOVERY_PACKAGE_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [],
            [
                child("source", ["0x0101", "0x0102", "0x0103", "0x0208"], "exactly_1"),
                child("input_component_manifest", ["0x0005"], "0..1"),
                child("wal", ["0x0204"], "0..16"),
            ],
        )
    )
    kinds.append(
        kind(
            "0x010d",
            "USER_SAVE_ORIGIN_MANIFEST_V1",
            65536,
            "SAVE_HARD",
            "strict_critical_tlv",
            "",
            [],
            [],
            notes="SAVE_HARD. Binds SaveCommitCertificate/Decision/Publication closure plus authority IdentityVerifierRef.",
        )
    )
    kinds.append(
        kind(
            "0x010e",
            "QUARANTINE_MANIFEST_V1",
            65536,
            "OPAQUE",
            "fixed_header_plus_entries",
            "flynes-quarantine-manifest-v1",
            [
                field("count", "u32be"),
                field("entries", "quarantine_entry[0..512]", note="fixed 116-byte entries"),
            ],
            [child("retained_blob", ["0x000e"], "0..n")],
            notes="Empty is u32be(0). Children are OPAQUE raw leaves. Never dispatched as claimed kind.",
        )
    )
    kinds.append(
        kind(
            "0x010f",
            "END_CLOSURE_MANIFEST_V1",
            1372,
            "END_HARD",
            "fixed_header_plus_entries",
            "flynes-end-closure-manifest-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("end_package_hash", "bytes32"),
                field("end_commit_decision_hash", "bytes32"),
                field("end_commit_certificate_hash", "bytes32"),
                field("authority_identity_verifier_ref", "IdentityVerifierRefV1[112]"),
                field("entry_count", "u32be"),
                field("entries", "closure_entry[0..32]", note="object_kind u16be || reserved_zero u16 || object_hash[32]"),
            ],
            [],
            fixed_length=None,
            notes="Prefix 220 bytes + 36*n, n=0..32, max 1372. Direct-ref list of END_HARD and SOFT_SAVE_ROOT. n=0/32 structurally valid but not a real three-root set.",
        )
    )
    kinds.append(
        kind(
            "0x0110",
            "PUBLISHED_SAVE_INDEX_V1",
            41040,
            "HARD",
            "fixed_header_plus_entries",
            "flynes-published-save-index-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("session_id", "u8[16]"),
                field("lineage_id", "u8[16]"),
                field("branch_id", "u8[16]"),
                field("authority_term", "u64be"),
                field("save_high_water", "u64be"),
                field("entry_count", "u32be"),
                field("reserved_zero_2", "u8[4]"),
                field("entries", "index_entry[0..1024]", note="save_revision u64be || origin_manifest_hash[32]"),
            ],
            [child("origin_manifest_hash", ["0x010d"], "0..1024")],
            notes="Header 80 + 40*n, max 41040. Body HARD; entries SOFT_SAVE_ROOT. n=0 and n=1024 legal.",
        )
    )
    kinds.append(
        kind(
            "0x0111",
            "OFFLINE_SAVE_MANIFEST_V1",
            65536,
            "OFFLINE_SAVE_HARD",
            "strict_critical_tlv",
            "flynes-offline-save-manifest-v1",
            [],
            [
                child("offline_progress_point_hash", ["0x0109"], "exactly_1"),
                child("runtime_checkpoint_hash", ["0x0001"], "exactly_1"),
                child("sram_blob_hash", ["0x0003"], "exactly_1"),
                child("user_save_payload_hash", ["0x000c"], "exactly_1"),
            ],
        )
    )
    kinds.append(
        kind(
            "0x0112",
            "OPEN_INPUT_RESERVATION_SET_V1",
            1576,
            "HARD",
            "fixed_header_plus_entries",
            "flynes-open-input-reservation-set-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("session_id", "u8[16]"),
                field("branch_id", "u8[16]"),
                field("authority_term", "u64be"),
                field("logical_seat", "u8"),
                field("reserved_zero_2", "u8[7]"),
                field("seat_revision", "u64be"),
                field("current_ledger_hash", "bytes32"),
                field("entry_count", "u32be"),
                field("reserved_zero_3", "u8[4]"),
                field("entries", "open_entry[0..8]", note="fixed 184-byte entries"),
            ],
            [
                child("current_ledger_hash", ["0x0202"], "exactly_1"),
                child("reservation", ["0x0203"], "per_entry_1"),
                child("component", ["0x0008"], "per_entry_1"),
                child("grant", ["0x020a"], "per_entry_1"),
                child("ack", ["0x020b"], "per_entry_0or1"),
                child("finalized", ["0x020c"], "per_entry_0or1"),
            ],
            enums={
                "state": {"PENDING": 1, "ACKED": 2, "OPEN": 3},
                "use_scope": {"ACTIVE_INPUT": 1, "TRANSITION_PRIME": 2},
            },
            notes="Header 104 + 184*count, max 1576. PENDING no ack/finalized; ACKED ack only; OPEN both.",
        )
    )
    kinds.append(
        kind(
            "0x0201",
            "DUAL_RUN_FENCE_V1",
            232,
            "HARD",
            "fixed",
            "flynes-dual-run-fence-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("session_id", "u8[16]"),
                field("lineage_id", "u8[16]"),
                field("branch_id", "u8[16]"),
                field("authority_term", "u64be"),
                field("timeline_epoch", "u64be"),
                field("run_generation", "u64be"),
                field("fence_owner_pair_role", "u8"),
                field("state", "u8"),
                field("activation_kind", "u8"),
                field("reserved_zero_2", "u8[5]"),
                field("activation_decision_hash_or_zero", "bytes32"),
                field("safe_cursor", "FrameCursorV1[16]"),
                field("safe_recovery_point_hash", "bytes32"),
                field("safe_input_root", "bytes32"),
                field("session_config_hash", "bytes32"),
            ],
            [child("safe_recovery_point_hash", ["0x0101"], "exactly_1")],
            enums={
                "state": {"QUIESCED": 1, "ARMED": 2},
                "activation_kind": {
                    "NONE": 0,
                    "INITIAL": 1,
                    "RUN_RESUME": 2,
                    "RESYNC": 3,
                    "AUTO_BARRIER": 4,
                },
                "pair_role": {"INITIATOR": 1, "RESPONDER": 2},
            },
            fixed_length=232,
        )
    )
    kinds.append(
        kind(
            "0x0202",
            "INPUT_SEQUENCE_LEDGER_V1",
            120,
            "HARD",
            "fixed",
            "flynes-input-sequence-ledger-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("session_id", "u8[16]"),
                field("logical_seat", "u8"),
                field("reserved_zero_2", "u8[7]"),
                field("current_owner_key_id", "bytes32"),
                field("current_seat_revision", "u64be"),
                field("reserved_through", "u64be"),
                field("ledger_generation", "u64be"),
                field("last_reservation_hash", "bytes32"),
            ],
            [],
            fixed_length=120,
        )
    )
    kinds.append(
        kind(
            "0x0203",
            "INPUT_SEQUENCE_RESERVATION_V1",
            144,
            "HARD",
            "fixed",
            "flynes-input-sequence-reservation-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("session_id", "u8[16]"),
                field("logical_seat", "u8"),
                field("kind", "u8"),
                field("reserved_zero_2", "u8[6]"),
                field("seat_revision", "u64be"),
                field("timeline_epoch", "u64be"),
                field("owner_key_id", "bytes32"),
                field("ledger_generation", "u64be"),
                field("first", "u64be"),
                field("last", "u64be"),
                field("prev_reserved", "u64be"),
                field("request_nonce", "bytes32"),
            ],
            [],
            enums={
                "kind": {
                    "NORMAL_BLOCK": 1,
                    "PRIME": 2,
                    "TERMINAL": 3,
                    "AUTHORITY_CLEAR": 4,
                }
            },
            fixed_length=144,
        )
    )
    kinds.append(
        kind(
            "0x0204",
            "SESSION_WAL_RECORD_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [
                field("dependency_count", "u16be"),
                field("reserved_zero", "u16be"),
                field("dependencies", "wal_dependency[0..32]", note="relation u16be || object_kind u16be || object_hash[32]"),
            ],
            [child("RECOVERY_LANDMARK", ["0x0204"], "0..3")],
            enums={
                "relation": {
                    "PRE_SAFE": 1,
                    "TRANSITION_PACKAGE": 2,
                    "RECOVERY_HEAD": 3,
                    "RECOVERY_POINT": 4,
                    "TAIL_COMPONENT": 5,
                    "EPOCH_PRIME": 6,
                    "SESSION_START_PACKAGE": 7,
                    "SOURCE_PROGRESS_REF": 8,
                    "OFFLINE_RELEASE": 9,
                    "SAVE_ORIGIN": 10,
                    "FORK_HANDOFF": 11,
                    "OPAQUE_EVIDENCE": 12,
                    "OPERATION_PAYLOAD": 13,
                    "RUN_FENCE": 14,
                    "SESSION_CONFIG": 15,
                    "COORDINATION_EVIDENCE": 16,
                    "PRIME_AUTHORIZATION": 17,
                    "EPOCH_INPUT_CLOSE": 18,
                    "INPUT_RANGE_AUTHORIZATION": 19,
                    "RECOVERY_LANDMARK": 20,
                }
            },
            notes="Unknown critical tag rejects. Relation 20 may typed-reference earlier 0x0204 (max 3, earlier phase only).",
        )
    )
    kinds.append(
        kind(
            "0x0205",
            "RECONNECT_DEADLINE_RECORD_V1",
            144,
            "HARD",
            "fixed",
            "flynes-reconnect-deadline-record-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("session_id", "u8[16]"),
                field("branch_id", "u8[16]"),
                field("authority_term", "u64be"),
                field("last_committed_reconnect_counter", "u64be"),
                field("reconnect_high_water", "u64be"),
                field("last_authenticated_activity_ns", "u64be"),
                field("reconnect_deadline_ns", "u64be"),
                field("continuous_clock_generation_hash", "bytes32"),
                field("session_config_hash", "bytes32"),
            ],
            [],
            fixed_length=144,
        )
    )
    kinds.append(
        kind(
            "0x0206",
            "PREPREPARE_TERMINAL_EVIDENCE_V1",
            272,
            "HARD",
            "fixed",
            "flynes-preprepare-terminal-evidence-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("session_id", "u8[16]"),
                field("branch_id", "u8[16]"),
                field("authority_term", "u64be"),
                field("active_manifest_hash", "bytes32"),
                field("deadline_record_evidence_hash_or_zero", "bytes32"),
                field("stored_clock_generation_hash_or_zero", "bytes32"),
                field("observed_clock_generation_hash", "bytes32"),
                field("observed_now_ns_or_raw_length", "u64be"),
                field("source_progress_evidence_hash", "bytes32"),
                field("pending_transition_id_or_zero", "u8[16]"),
                field("pending_transition_record_hash_or_zero", "bytes32"),
                field("terminal_kind", "u8"),
                field("deadline_record_status", "u8"),
                field("source_evidence_kind", "u8"),
                field("pending_transition_kind", "u8"),
                field("pending_transition_phase", "u8"),
                field("reserved_zero_2", "u8[3]"),
            ],
            [
                child("active_manifest_hash", ["0x0104"], "exactly_1"),
                child("source", ["0x0102", "0x0103", "0x0208"], "exactly_1"),
                child("pending", ["0x0204"], "0..1"),
                child("deadline_valid", ["0x0205"], "0..1"),
                child("deadline_corrupt_bounded", ["0x000d"], "0..1"),
            ],
            enums={
                "deadline_record_status": {
                    "VALID": 1,
                    "MISSING": 2,
                    "CORRUPT_BOUNDED": 3,
                    "CORRUPT_OVERSIZE": 4,
                },
                "terminal_kind": {
                    "DEADLINE_ELAPSED": 1,
                    "CLOCK_GENERATION_CHANGED": 2,
                    "DEADLINE_RECORD_MISSING": 3,
                    "DEADLINE_RECORD_CORRUPT": 4,
                },
                "source_evidence_kind": {
                    "AUTHORITY_HEAD": 1,
                    "REPLAYABLE_PROOF": 2,
                    "OPAQUE_FAILURE": 3,
                },
            },
            fixed_length=272,
        )
    )
    kinds.append(
        kind(
            "0x0207",
            "TERMINAL_RECOVERY_EVIDENCE_V1",
            344,
            "HARD",
            "fixed",
            "flynes-terminal-recovery-evidence-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("session_id", "u8[16]"),
                field("branch_id", "u8[16]"),
                field("authority_term", "u64be"),
                field("frozen_active_manifest_hash", "bytes32"),
                field("attempt_reconnect_counter", "u64be"),
                field("attempt_id", "u8[16]"),
                field("channel_id", "u8[16]"),
                field("reconnect_transcript_hash", "bytes32"),
                field("both_prepared_record_hash", "bytes32"),
                field("recovery_work_decision_hash_or_zero", "bytes32"),
                field("last_phase_record_hash", "bytes32"),
                field("source_progress_evidence_hash", "bytes32"),
                field("pending_transition_id_or_zero", "u8[16]"),
                field("pending_transition_record_hash_or_zero", "bytes32"),
                field("terminal_kind", "u8"),
                field("recovery_work_kind", "u8"),
                field("last_phase", "u8"),
                field("activation_bound", "u8"),
                field("source_evidence_kind", "u8"),
                field("pending_transition_kind", "u8"),
                field("pending_transition_phase", "u8"),
                field("missing_record_mask", "u8"),
                field("corrupt_record_mask", "u8"),
                field("reserved_zero_2", "u8[7]"),
            ],
            [
                child("frozen_active_manifest_hash", ["0x0104"], "exactly_1"),
                child("source", ["0x0102", "0x0103", "0x0208"], "exactly_1"),
                child("wal_records", ["0x0204"], "0..4"),
            ],
            enums={
                "recovery_work_kind": {
                    "NONE": 0,
                    "RUN_RESUME": 1,
                    "MANDATORY_MODE_SWITCH": 2,
                    "STAY_PAUSED": 3,
                    "PREACTIVE_CONVERGENCE": 4,
                },
                "source_evidence_kind": {
                    "AUTHORITY_HEAD": 1,
                    "REPLAYABLE_PROOF": 2,
                    "OPAQUE_FAILURE": 3,
                },
            },
            fixed_length=344,
        )
    )
    kinds.append(
        kind(
            "0x0208",
            "OPAQUE_RECOVERY_FAILURE_EVIDENCE_V1",
            192,
            "HARD",
            "fixed",
            "flynes-opaque-recovery-failure-evidence-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("session_id", "u8[16]"),
                field("branch_id", "u8[16]"),
                field("authority_term", "u64be"),
                field("active_manifest_hash", "bytes32"),
                field("expected_source_hash_or_zero", "bytes32"),
                field("quarantine_manifest_hash", "bytes32"),
                field("observed_failure_hash", "bytes32"),
                field("local_role", "u8"),
                field("failure_kind", "u8"),
                field("reserved_zero_2", "u8[14]"),
            ],
            [
                child("active_manifest_hash", ["0x0104"], "exactly_1"),
                child("quarantine_manifest_hash", ["0x010e"], "exactly_1"),
            ],
            enums={
                "local_role": {"AUTHORITY": 1, "PEER": 2},
                "failure_kind": {
                    "SOURCE_MISSING": 1,
                    "HASH_MISMATCH": 2,
                    "SEMANTIC_INVALID": 3,
                    "CONTENT_UNAVAILABLE": 4,
                    "STORAGE_UNREADABLE": 5,
                    "PENDING_TRANSITION_UNRESOLVED": 6,
                    "CONTROL_RECORD_MISSING": 7,
                    "CONTROL_RECORD_CORRUPT": 8,
                },
            },
            fixed_length=192,
        )
    )
    kinds.append(
        kind(
            "0x0209",
            "IDENTITY_VERIFIER_REF_V1",
            112,
            "HARD",
            "fixed",
            "flynes-identity-verifier-ref-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("key_id", "bytes32"),
                field("identity_public_key_x963", "u8[65]"),
                field("reserved_zero_2", "u8[7]"),
            ],
            [],
            fixed_length=112,
            notes="Public key starts 0x04, on P-256. key_id=SHA256(\"flynes-identity-key-id-v1\" || u32be(65) || exact_x963).",
        )
    )
    kinds.append(
        kind(
            "0x020a",
            "INPUT_RESERVATION_GRANT_V1",
            312,
            "HARD",
            "fixed",
            "flynes-input-sequence-reserve-grant-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("request_hash", "bytes32"),
                field("before_ledger_hash", "bytes32"),
                field("after_ledger_hash", "bytes32"),
                field("reservation", "InputSequenceReservationV1[144]"),
                field("reservation_hash", "bytes32"),
                field("reservation_component_hash", "bytes32"),
            ],
            [
                child("request_hash", ["0x020d"], "exactly_1"),
                child("before_ledger_hash", ["0x0202"], "exactly_1"),
                child("after_ledger_hash", ["0x0202"], "exactly_1"),
            ],
            fixed_length=312,
        )
    )
    kinds.append(
        kind(
            "0x020b",
            "INPUT_RESERVATION_ACK_V1",
            192,
            "HARD",
            "fixed",
            "flynes-input-sequence-reserve-ack-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("session_id", "u8[16]"),
                field("logical_seat", "u8"),
                field("reserved_zero_2", "u8[7]"),
                field("owner_key_id", "bytes32"),
                field("request_hash", "bytes32"),
                field("grant_hash", "bytes32"),
                field("after_ledger_hash", "bytes32"),
                field("reservation_component_hash", "bytes32"),
            ],
            [],
            fixed_length=192,
        )
    )
    kinds.append(
        kind(
            "0x020c",
            "INPUT_RESERVATION_FINALIZED_V1",
            248,
            "HARD",
            "fixed",
            "flynes-input-sequence-reserve-finalized-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("session_id", "u8[16]"),
                field("branch_id", "u8[16]"),
                field("authority_term", "u64be"),
                field("logical_seat", "u8"),
                field("reserved_zero_2", "u8[7]"),
                field("owner_key_id", "bytes32"),
                field("request_hash", "bytes32"),
                field("grant_hash", "bytes32"),
                field("replica_ack_hash", "bytes32"),
                field("after_ledger_hash", "bytes32"),
                field("reservation_component_hash", "bytes32"),
            ],
            [],
            fixed_length=248,
        )
    )
    kinds.append(
        kind(
            "0x020d",
            "INPUT_RESERVATION_REQUEST_V1",
            184,
            "HARD",
            "fixed",
            "flynes-input-sequence-reserve-req-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("session_id", "u8[16]"),
                field("logical_seat", "u8"),
                field("kind", "u8"),
                field("reserved_zero_2", "u8[6]"),
                field("seat_revision", "u64be"),
                field("timeline_epoch", "u64be"),
                field("owner_key_id", "bytes32"),
                field("requested_count", "u32be"),
                field("reserved_zero_3", "u8[4]"),
                field("request_nonce", "bytes32"),
                field("before_ledger_hash", "bytes32"),
                field("authorization_hash_or_zero", "bytes32"),
            ],
            [
                child("authorization_prime", ["0x020e"], "0..1"),
                child("authorization_range", ["0x0211"], "0..1"),
            ],
            enums={
                "kind": {
                    "NORMAL_BLOCK": 1,
                    "PRIME": 2,
                    "TERMINAL": 3,
                    "AUTHORITY_CLEAR": 4,
                }
            },
            fixed_length=184,
        )
    )
    kinds.append(
        kind(
            "0x020e",
            "PRIME_AUTHORIZATION_V1",
            240,
            "HARD",
            "fixed",
            "flynes-prime-authorization-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("authorization_kind", "u8"),
                field("prime_source_kind", "u8"),
                field("clear_reason", "u8"),
                field("input_delay_frames", "u8"),
                field("logical_seat", "u8"),
                field("reserved_zero_2", "u8[3]"),
                field("transaction_id", "u8[16]"),
                field("session_id", "u8[16]"),
                field("branch_id", "u8[16]"),
                field("authority_term", "u64be"),
                field("base_recovery_point_hash", "bytes32"),
                field("epoch_input_close_hash_or_zero", "bytes32"),
                field("target_session_config_hash", "bytes32"),
                field("base_cursor", "FrameCursorV1[16]"),
                field("target_timeline_epoch", "u64be"),
                field("target_seat_revision", "u64be"),
                field("target_owner_key_id", "bytes32"),
                field("requested_count", "u32be"),
                field("full_mask", "u32be"),
            ],
            [
                child("base_recovery_point_hash", ["0x0101"], "exactly_1"),
                child("target_session_config_hash", ["0x0105"], "exactly_1"),
                child("epoch_input_close_hash", ["0x020f"], "0..1"),
            ],
            enums={
                "authorization_kind": {
                    "SESSION_START": 1,
                    "AUTO_BARRIER": 2,
                    "RUN_RESUME": 3,
                    "MODE_SWITCH": 4,
                },
                "prime_source_kind": {"HELD_PRIME": 1, "PORT_CLEAR": 2},
                "clear_reason": {
                    "NONE": 0,
                    "STOP": 1,
                    "PAUSE": 2,
                    "PEER_DISCONNECT": 3,
                    "SEAT_REASSIGN": 4,
                },
            },
            fixed_length=240,
        )
    )
    kinds.append(
        kind(
            "0x020f",
            "EPOCH_INPUT_CLOSE_CERTIFICATE_V1",
            296,
            "HARD",
            "fixed",
            "flynes-epoch-input-close-certificate-v1",
            [
                field("body", "EpochInputCloseBodyV1[168]"),
                field("initiator_signature", "u8[64]"),
                field("responder_signature", "u8[64]"),
            ],
            [child("final_ledger_hash", ["0x0202"], "exactly_1")],
            enums={"close_kind": {"AUTO_BARRIER": 1, "RUN_RESUME": 2, "MODE_SWITCH": 3}},
            fixed_length=296,
            notes="Body is version u16be || reserved_zero[6] || transaction_id[16] || session_id[16] || branch_id[16] || authority_term u64be || logical_seat u8 || close_kind u8 || reserved_zero[6] || old_timeline_epoch u64be || old_seat_revision u64be || old_open_reservation_set_hash[32] || final_ledger_hash[32] || prior_reserved_through u64be || closed_entry_count u32be || reserved_zero[4].",
        )
    )
    kinds.append(
        kind(
            "0x0210",
            "SUSPEND_INTENT_V1",
            240,
            "HARD",
            "fixed",
            "flynes-suspend-intent-v1",
            [
                field("pretag", "u8[176]"),
                field("signature", "u8[64]"),
            ],
            [child("reservation_component_hash", ["0x0008"], "exactly_1")],
            fixed_length=240,
            notes="Pretag: version u16be || reserved_zero[6] || session_id[16] || branch_id[16] || authority_term u64be || timeline_epoch u64be || seat_revision u64be || logical_seat u8 || reserved_zero[7] || owner_key_id[32] || first_target u64be || max_count u32be || terminal_full_mask u32be || first_input_sequence u64be || reservation_component_hash[32] || nonce[16]. Signature digest domain flynes-suspend-intent-signature-v1.",
        )
    )
    kinds.append(
        kind(
            "0x0211",
            "INPUT_RANGE_AUTHORIZATION_V1",
            336,
            "HARD",
            "fixed",
            "flynes-input-range-authorization-v1",
            [
                field("pretag", "u8[272]"),
                field("signature", "u8[64]"),
            ],
            [
                child("reservation_component_hash", ["0x0008"], "exactly_1"),
                child("before_ledger_hash", ["0x0202"], "0..1"),
                child("source_suspend_intent_hash", ["0x0210"], "0..1"),
            ],
            enums={
                "authorization_kind": {
                    "DIRECT_TERMINAL": 1,
                    "SUSPEND_DERIVED_TERMINAL": 2,
                    "AUTHORITY_CLEAR": 3,
                }
            },
            fixed_length=336,
        )
    )
    kinds.append(
        kind(
            "0x0212",
            "SESSION_SIGNING_KEY_BINDING_V1",
            312,
            "HARD",
            "fixed",
            "flynes-session-signing-key-binding-hash-v1",
            [
                field("pretag", "u8[248]"),
                field("signature", "u8[64]"),
            ],
            [],
            fixed_length=312,
            notes="Pretag embeds IdentityVerifierRefV1; no child objects. Signature digest domain flynes-session-signing-key-binding-v1.",
        )
    )
    kinds.append(
        kind(
            "0x0213",
            "PAIR_TRANSCRIPT_V1",
            880,
            "HARD",
            "fixed",
            "flynes-pair-transcript-object-v1",
            [
                field("preimage", "PairTranscriptPreimageV1[752]"),
                field("initiator_signature", "u8[64]"),
                field("responder_signature", "u8[64]"),
            ],
            [],
            enums={"entry_mode": {"BLE_SAS": 1, "QR": 2}},
            fixed_length=880,
            notes="pair_transcript_hash uses flynes-pair-transcript-v1 over the 752-byte preimage only. Object hash covers all 880 bytes. BLE/QR/Wi-Fi/friends not implemented in M1.",
        )
    )
    # 0x0216 / 0x0217: the link control plane (W0 frozen contract,
    # shared/src/session/link/link_control_contract.hpp). Sizes and offsets below
    # are the corrected 2026-09-16 layout: channel_id is the real 16-byte
    # wire::derive_channel_id_v1 value and there is no u64 channel_bind_id.
    kinds.append(
        kind(
            "0x0216",
            "LINK_HELLO_V1",
            488,
            "HARD",
            "fixed",
            "flynes-link-hello-object-v1",
            [
                field("pretag", "u8[424]"),
                field("signature", "u8[64]"),
            ],
            [],
            fixed_length=488,
            notes="Sender signature digest uses flynes-link-hello-v1 over the 424-byte pretag only. Pretag layout: version u16be || reserved_zero[6] || sender_role u8 || receiver_role u8 || phase u8 || reserved_zero[5] || session_id[16] || link_id[16] || channel_id[16] || connection_generation u64be || link_generation u64be || wire_major u16be || wire_minor u16be || capability_bits u16be || critical_extension_mask u16be || determinism_profile u8 || core_state_format u8 || reserved_zero[2] || selected_plan_hash[32] || endpoint_offer_hash[32] || pair_transcript_object_hash[32] || session_signing_binding_hash[32] || identity_verifier_ref[112] || session_signing_public_key[65] || reserved_zero[27].",
        )
    )
    kinds.append(
        kind(
            "0x0217",
            "LINK_READY_V1",
            432,
            "HARD",
            "fixed",
            "flynes-link-ready-object-v1",
            [
                field("pretag", "u8[368]"),
                field("signature", "u8[64]"),
            ],
            [],
            fixed_length=432,
            notes="Same bytes carry both READY (ready_phase=1) and the required ACK (ready_phase=2). Sender signature digest uses flynes-link-ready-v1 over the 368-byte pretag only. Pretag layout: version u16be || reserved_zero[6] || sender_role u8 || receiver_role u8 || phase u8 || ready_phase u8 || reserved_zero[4] || session_id[16] || link_id[16] || channel_id[16] || connection_generation u64be || reconnect_attempt u64be || link_generation u64be || channel_bind_hash[32] || local_hello_object_hash[32] || peer_hello_object_hash[32] || negotiated_result_hash[32] || local_summary_hash[32] || peer_summary_hash[32] || merge_result_hash[32] || reserved_zero[56].",
        )
    )
    kinds.append(
        kind(
            "0x0218",
            "PENDING_CONFIG_CONFIRM_V1",
            44,
            "HARD",
            "fixed",
            "flynes-pending-config-confirm-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[2]"),
                field("pending_config_id", "u8[32]"),
                field("pending_config_revision", "u64be"),
            ],
            [],
            fixed_length=44,
            notes="Control-only after LINK_READY. Codec rejects zero id and zero revision. Dual maps id/revision mismatch to STALE and a repeated (id, revision) to DUPLICATE. Authentication is the existing Control stream, not a new signature.",
        )
    )
    kinds.append(
        kind(
            "0x0301",
            "OFFLINE_RELEASE_CERTIFICATE_V1",
            312,
            "HARD",
            "fixed",
            "flynes-offline-release-certificate-hash-v1",
            [
                field("body", "OfflineReleaseCertificateBodyV1[248]"),
                field("signature", "u8[64]"),
            ],
            [child("session_start_package", ["0x010a"], "exactly_1"), child("source_progress_ref", ["0x0108"], "exactly_1")],
            fixed_length=312,
            notes="Signature digest domain flynes-offline-release-certificate-v1 over the 248-byte body.",
        )
    )
    kinds.append(
        kind(
            "0x0302",
            "FORK_COMMIT_DECISION_V1",
            65536,
            "HARD",
            "strict_critical_tlv",
            "",
            [],
            [],
            notes="HARD decision. Tags not privately assigned.",
        )
    )
    kinds.append(
        kind(
            "0x0303",
            "SAVE_COMMIT_CERTIFICATE_V1",
            392,
            "SAVE_HARD",
            "fixed",
            "flynes-save-commit-certificate-hash-v1",
            [
                field("body", "SaveCommitCertificateBodyV1[328]"),
                field("signature", "u8[64]"),
            ],
            [],
            fixed_length=392,
            notes="Signature digest domain flynes-save-commit-certificate-v1.",
        )
    )
    kinds.append(
        kind(
            "0x0304",
            "SAVE_COMMIT_DECISION_V1",
            208,
            "SAVE_HARD",
            "fixed",
            "flynes-save-commit-decision-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("save_decision_id", "u8[16]"),
                field("enclosing_kind", "u8"),
                field("reserved_zero_2", "u8[7]"),
                field("enclosing_prepare_or_package_hash", "bytes32"),
                field("session_id", "u8[16]"),
                field("branch_id", "u8[16]"),
                field("authority_term", "u64be"),
                field("timeline_epoch", "u64be"),
                field("save_cursor", "FrameCursorV1[16]"),
                field("save_revision", "u64be"),
                field("user_save_payload_hash", "bytes32"),
                field("save_commit_certificate_hash", "bytes32"),
                field("save_high_water_after", "u64be"),
            ],
            [],
            enums={"enclosing_kind": {"ZERO_STEP": 1, "END": 2}},
            fixed_length=208,
        )
    )
    kinds.append(
        kind(
            "0x0305",
            "SAVE_PUBLICATION_CERTIFICATE_V1",
            400,
            "SAVE_HARD",
            "fixed",
            "flynes-save-publication-certificate-hash-v1",
            [
                field("body", "SavePublicationCertificateBodyV1[336]"),
                field("signature", "u8[64]"),
            ],
            [],
            fixed_length=400,
            notes="Signature digest domain flynes-save-publication-certificate-v1.",
        )
    )
    kinds.append(
        kind(
            "0x0306",
            "END_PACKAGE_V1",
            65536,
            "END",
            "strict_critical_tlv",
            "flynes-end-package-v1",
            [
                field("schema_version", "u16be"),
                field("field_count", "u16be"),
                field("tlv", "tag_length_value[23]"),
            ],
            [
                child("active_manifest_hash", ["0x0104"], "exactly_1"),
                child("recovery_evidence_hash", ["0x0102", "0x0208"], "exactly_1"),
                child("runtime_checkpoint_hash", ["0x0001"], "0..1"),
                child("sram_blob_hash", ["0x0003"], "0..1"),
                child("final_save_certificate_hash", ["0x0303"], "0..1"),
                child("coordination_evidence_hash", ["0x0206", "0x0207"], "0..1"),
            ],
            notes="u16be(1)||u16be(23) then tags 1..23 strictly increasing as u16be(tag)||u32be(length)||value. Unknown/duplicate/out-of-order/trailing rejects. Not a closure entry.",
        )
    )
    kinds.append(
        kind(
            "0x0307",
            "END_COMMIT_DECISION_V1",
            360,
            "END",
            "fixed",
            "flynes-end-commit-decision-v1",
            [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("end_decision_id", "u8[16]"),
                field("end_package_hash", "bytes32"),
                field("end_commit_certificate_hash", "bytes32"),
                field("session_id", "u8[16]"),
                field("lineage_id", "u8[16]"),
                field("branch_id", "u8[16]"),
                field("authority_term", "u64be"),
                field("timeline_epoch", "u64be"),
                field("authority_key_id", "bytes32"),
                field("ownership_generation", "u64be"),
                field("authority_writer_generation_before", "u64be"),
                field("authority_writer_generation_after", "u64be"),
                field("sram_high_water_after", "u64be"),
                field("save_high_water_after", "u64be"),
                field("active_manifest_before_hash", "bytes32"),
                field("save_commit_decision_hash_or_zero", "bytes32"),
                field("save_publication_certificate_hash_or_zero", "bytes32"),
                field("save_origin_manifest_hash_or_zero", "bytes32"),
                field("coordination_mode", "u8"),
                field("save_policy", "u8"),
                field("end_reason", "u16be"),
                field("reserved_zero_2", "u8[4]"),
            ],
            [
                child("save_commit_decision_hash", ["0x0304"], "0..1"),
                child("save_publication_certificate_hash", ["0x0305"], "0..1"),
                child("save_origin_manifest_hash", ["0x010d"], "0..1"),
            ],
            enums={
                "coordination_mode": {
                    "CONNECTED": 1,
                    "AUTHORITY_AFTER_PREPARE_GAP": 2,
                    "AUTHORITY_AFTER_TERMINAL_RECOVERY": 3,
                },
                "save_policy": {"SAVE": 1, "DISCARD": 2, "RECOVERY_ONLY": 3},
            },
            fixed_length=360,
            notes="END root; not a closure entry.",
        )
    )
    kinds.append(
        kind(
            "0x0308",
            "END_COMMIT_CERTIFICATE_V1",
            392,
            "END",
            "fixed",
            "flynes-end-commit-certificate-hash-v1",
            [
                field("body", "EndCommitCertificateBodyV1[328]"),
                field("signature", "u8[64]"),
            ],
            [
                child("save_commit_decision_hash", ["0x0304"], "0..1"),
                child("save_publication_certificate_hash", ["0x0305"], "0..1"),
                child("save_origin_manifest_hash", ["0x010d"], "0..1"),
            ],
            fixed_length=392,
            notes="END root; not a closure entry. Signature digest domain flynes-end-commit-certificate-v1.",
        )
    )

    messages = [
        {
            "endian": "network",
            "fields": [
                field("kind", "u8", note="GENESIS=0 FRAME=1"),
                field("reserved_zero", "u8[7]"),
                field("index", "u64be"),
            ],
            "fixed_length": 16,
            "hash_domain": "",
            "name": "FrameCursorV1",
            "notes": "GENESIS index must be 0. Unknown kind rejects.",
        },
        {
            "endian": "network",
            "fields": [
                field("kind", "u8", note="NONE=0 PRESENT=1"),
                field("reserved_zero", "u8[7]"),
                field("timeline_epoch", "u64be", cardinality="present_only"),
                field("frame_cursor", "FrameCursorV1[16]", cardinality="present_only"),
                field("evidence_hash", "bytes32", cardinality="present_only"),
            ],
            "fixed_lengths": {"NONE": 8, "PRESENT": 64},
            "hash_domain": "",
            "name": "EvidenceCursorV1",
            "notes": "NONE has no trailing epoch/cursor/hash.",
        },
        {
            "endian": "network",
            "fields": [
                field("version", "u16be"),
                field("session_id", "u8[16]"),
                field("branch_id", "u8[16]"),
                field("authority_term", "u64be"),
                field("timeline_epoch", "u64be"),
                field("seat_revision", "u64be"),
                field("mode_generation", "u64be"),
                field("frame_index", "u64be"),
                field("batch_sequence", "u64be"),
                field("predicted_port_mask", "u8"),
                field("reserved_zero", "u8[7]"),
                field("ports", "port_slot[4]", note="source_kind u8 + clear_reason u8 + reserved_zero u16 + buttons u32be + input_sequence u64be"),
            ],
            "fixed_length": 154,
            "hash_domain": "flynes-input-bundle-v1",
            "hash_preimage": 'SHA256(0x10 || UTF8("flynes-input-bundle-v1") || u32be(len) || bundle_bytes)',
            "name": "CanonicalInputBundleV1",
            "enums": {
                "source_kind": {
                    "REAL_SAMPLE": 1,
                    "HELD_PRIME": 2,
                    "PORT_CLEAR": 3,
                    "NEUTRAL_UNASSIGNED": 4,
                    "PREDICTED": 5,
                    "TERMINAL_HOLD": 6,
                    "HELD_SAMPLE": 7,
                },
                "clear_reason": {
                    "NONE": 0,
                    "STOP": 1,
                    "PAUSE": 2,
                    "PEER_DISCONNECT": 3,
                    "SEAT_REASSIGN": 4,
                },
                "buttons": {
                    "A": 1,
                    "B": 2,
                    "SELECT": 4,
                    "START": 8,
                    "UP": 16,
                    "DOWN": 32,
                    "LEFT": 64,
                    "RIGHT": 128,
                },
            },
            "notes": "UP+DOWN and LEFT+RIGHT normalized to clear both bits before hash.",
        },
        {
            "endian": "network",
            "fields": [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("binding_kind", "u8"),
                field("reserved_zero_2", "u8[7]"),
                field("pair_transcript_hash", "bytes32"),
                field("session_id", "u8[16]"),
                field("reconnect_transcript_hash_or_zero", "bytes32"),
                field("reconnect_counter", "u64be"),
                field("channel_id", "u8[16]"),
                field("bind_nonce", "u8[16]"),
            ],
            "fixed_length": 136,
            "hash_domain": "",
            "name": "ChannelBindV1",
            "enums": {
                "binding_kind": {
                    "INITIAL": 1,
                    "SAME_PATH_RECONNECT": 2,
                    "NEW_BEARER_RECONNECT": 3,
                }
            },
            "notes": "INITIAL: reconnect hash/counter=0; bind_nonce=SHA256(\"flynes-initial-bind-v1\" || pair_transcript_hash)[0..15]. Not an ObjectKind.",
        },
        {
            "endian": "network",
            "fields": [
                field("body", "ChannelBindProofBodyV1[216]"),
                field("tag", "u8[32]"),
            ],
            "fixed_length": 248,
            "hash_domain": "flynes-channel-bind-proof-hash-v1",
            "name": "ChannelBindProofV1",
            "notes": "Not an ObjectKind. QUIC mapping only; no BLE/QR/Wi-Fi.",
        },
        {
            "endian": "network",
            "fields": [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("session_id", "u8[16]"),
                field("branch_id", "u8[16]"),
                field("authority_term", "u64be"),
                field("attempt_reconnect_counter", "u64be"),
                field("attempt_id", "u8[16]"),
                field("channel_id", "u8[16]"),
                field("sender_role", "u8"),
                field("receiver_role", "u8"),
                field("transition_present", "u8"),
                field("reserved_zero_2", "u8[5]"),
                field("local_active_manifest_hash", "bytes32"),
                field("local_transition_id", "u8[16]"),
                field("local_WAL_phase_hash", "bytes32"),
            ],
            "fixed_length": 176,
            "hash_domain": "flynes-channel-resume-summary-v1",
            "name": "ChannelResumeSummaryV1",
            "notes": "Not an ObjectKind. 176-byte exact-size wire message.",
        },
        {
            "endian": "network",
            "fields": [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("sender_role", "u8", note="1 initiator, 2 responder"),
                field("receiver_role", "u8", note="must be the mirrored role"),
                field("phase", "u8", note="INITIAL=1"),
                field("reserved_zero_2", "u8[5]"),
                field("session_id", "u8[16]"),
                field("link_id", "u8[16]"),
                field("channel_id", "u8[16]"),
                field("connection_generation", "u64be"),
                field("link_generation", "u64be"),
                field("wire_major", "u16be"),
                field("wire_minor", "u16be"),
                field("capability_bits", "u16be"),
                field("critical_extension_mask", "u16be", note="MUST be 0"),
                field("determinism_profile", "u8"),
                field("core_state_format", "u8"),
                field("reserved_zero_3", "u8[2]"),
                field("selected_plan_hash", "bytes32"),
                field("endpoint_offer_hash", "bytes32"),
                field("pair_transcript_object_hash", "bytes32"),
                field("session_signing_binding_hash", "bytes32"),
                field("identity_verifier_ref", "u8[112]"),
                field("session_signing_public_key", "u8[65]"),
                field("reserved_zero_4", "u8[27]"),
            ],
            "fixed_length": 424,
            "hash_domain": "flynes-link-hello-v1",
            "name": "LinkHelloV1",
            "enums": {
                "sender_role": {"INITIATOR": 1, "RESPONDER": 2},
                "phase": {"INITIAL": 1, "RECONCILE": 2},
                "capability_bits": {"DUAL": 1, "STREAM_VIDEO": 2, "STREAM_AUDIO": 4},
            },
            "notes": "Control-channel message tag 0xFF06; the 64-byte canonical low-S signature is appended after this 424-byte pretag, so the persisted LINK_HELLO_V1 object (kind 0x0216) is exactly 488 bytes. Persist-before-send: the 0x0212 binding object must be durable first. STREAM bits are never advertised by this release and a STREAM-only offer is rejected as unsupported.",
        },
        {
            "endian": "network",
            "fields": [
                field("version", "u16be"),
                field("reserved_zero", "u8[6]"),
                field("sender_role", "u8", note="1 initiator, 2 responder"),
                field("receiver_role", "u8", note="must be the mirrored role"),
                field("phase", "u8", note="RECONCILE=2"),
                field("ready_phase", "u8", note="READY=1 ACK=2"),
                field("reserved_zero_2", "u8[4]"),
                field("session_id", "u8[16]"),
                field("link_id", "u8[16]"),
                field("channel_id", "u8[16]"),
                field("connection_generation", "u64be"),
                field("reconnect_attempt", "u64be"),
                field("link_generation", "u64be"),
                field("channel_bind_hash", "bytes32", note="domain flynes-channel-bind-binding-v1"),
                field("local_hello_object_hash", "bytes32"),
                field("peer_hello_object_hash", "bytes32"),
                field("negotiated_result_hash", "bytes32"),
                field("local_summary_hash", "bytes32"),
                field("peer_summary_hash", "bytes32"),
                field("merge_result_hash", "bytes32"),
                field("reserved_zero_3", "u8[56]"),
            ],
            "fixed_length": 368,
            "hash_domain": "flynes-link-ready-v1",
            "name": "LinkReadyV1",
            "enums": {
                "sender_role": {"INITIATOR": 1, "RESPONDER": 2},
                "phase": {"INITIAL": 1, "RECONCILE": 2},
                "ready_phase": {"READY": 1, "ACK": 2},
            },
            "notes": "Control-channel message tag 0xFF07; the 64-byte canonical low-S signature is appended after this 368-byte pretag, so the persisted LINK_READY_V1 object (kind 0x0217) is exactly 432 bytes. The same bytes carry the required ACK with ready_phase=ACK. READY is legal only in phase RECONCILE.",
        },
    ]

    end_package_tags = [
        {"tag": 1, "name": "transaction_id", "wire_type": "u8[16]"},
        {"tag": 2, "name": "coordination_mode", "wire_type": "u8"},
        {"tag": 3, "name": "session_id", "wire_type": "u8[16]"},
        {"tag": 4, "name": "lineage_id", "wire_type": "u8[16]"},
        {"tag": 5, "name": "branch_id", "wire_type": "u8[16]"},
        {"tag": 6, "name": "authority_term", "wire_type": "u64be"},
        {"tag": 7, "name": "timeline_epoch", "wire_type": "u64be"},
        {"tag": 8, "name": "final_progress_evidence_cursor", "wire_type": "EvidenceCursorV1[8|64]"},
        {"tag": 9, "name": "active_manifest_hash", "wire_type": "bytes32"},
        {"tag": 10, "name": "recovery_evidence_hash", "wire_type": "bytes32"},
        {"tag": 11, "name": "runtime_checkpoint_hash", "wire_type": "bytes32"},
        {"tag": 12, "name": "state_digest", "wire_type": "bytes32"},
        {"tag": 13, "name": "sram_revision", "wire_type": "u64be"},
        {"tag": 14, "name": "sram_blob_hash", "wire_type": "bytes32"},
        {"tag": 15, "name": "save_policy", "wire_type": "u8"},
        {"tag": 16, "name": "save_revision_or_zero", "wire_type": "u64be"},
        {"tag": 17, "name": "final_save_hash_or_zero", "wire_type": "bytes32"},
        {"tag": 18, "name": "final_save_certificate_hash_or_zero", "wire_type": "bytes32"},
        {"tag": 19, "name": "end_reason", "wire_type": "u16be"},
        {"tag": 20, "name": "sram_high_water", "wire_type": "u64be"},
        {"tag": 21, "name": "save_high_water", "wire_type": "u64be"},
        {"tag": 22, "name": "coordination_evidence_hash_or_zero", "wire_type": "bytes32"},
        {"tag": 23, "name": "authority_key_id", "wire_type": "bytes32"},
    ]
    for item in kinds:
        if item["name"] == "END_PACKAGE_V1":
            item["tlv_tags"] = end_package_tags
            item["unknown_critical_tag"] = "reject"

    quic_channels = [
        {"id": 1, "name": "Control", "form": "reliable_stream"},
        {"id": 2, "name": "Input", "form": "datagram"},
        {"id": 3, "name": "StateCommit", "form": "reliable_stream"},
        {"id": 4, "name": "Bulk", "form": "reliable_stream"},
        {"id": 5, "name": "ROM", "form": "reliable_stream"},
        {"id": 6, "name": "Video", "form": "datagram"},
        {"id": 7, "name": "Audio", "form": "datagram"},
    ]

    hash_domains = sorted(
        {
            item["hash_domain"]
            for item in kinds
            if item["hash_domain"]
        }
        | {
            "flynes-session-schema-registry-v1",
            "flynes-identity-key-id-v1",
            "flynes-identity-verifier-ref-v1",
            "flynes-input-bundle-v1",
            "flynes-input-root-base-v1",
            "flynes-input-root-step-v1",
            "flynes-tail-component-v1",
            "flynes-channel-bind-proof-hash-v1",
            "flynes-channel-bind-proof-v1",
            "flynes-initial-bind-v1",
            "flynes-pair-transcript-v1",
            "flynes-pair-transcript-object-v1",
            "flynes-channel-resume-summary-v1",
            # Link control plane. The two object-hash domains arrive through the
            # 0x0216/0x0217 kind entries; the two signature-digest domains and
            # the canonical bind-binding domain are named here because they cover
            # a pretag/summary rather than a complete object.
            "flynes-link-hello-v1",
            "flynes-link-ready-v1",
            "flynes-link-negotiated-result-v1",
            "flynes-channel-bind-binding-v1",
        }
    )

    return {
        "hash_domain_strings": hash_domains,
        "illegal_kinds": ["0x0000"],
        "kinds": kinds,
        "messages": messages,
        "quic_channels": quic_channels,
        "schema_id": "flynes_session_v1",
        "unassigned_illegal": True,
        "unknown_critical_tlv": "reject",
        "unknown_optional_tlv": "skip",
        "wire_endian": "network",
        "wire_integer_endian": "big",
    }


def emit(schema: dict) -> bytes:
    text = json.dumps(schema, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
    return text.encode("utf-8").replace(b"\r\n", b"\n")


def registry_hash(schema_bytes: bytes) -> str:
    length = len(schema_bytes)
    preimage = REGISTRY_DOMAIN + length.to_bytes(4, "big") + schema_bytes
    return hashlib.sha256(preimage).hexdigest()


def main() -> int:
    schema_bytes = emit(build_schema())
    digest = registry_hash(schema_bytes)
    hash_bytes = (digest + "\n").encode("ascii")
    header = (
        "#ifndef FLYNES_SESSION_SCHEMA_REGISTRY_HASH_H\n"
        "#define FLYNES_SESSION_SCHEMA_REGISTRY_HASH_H\n\n"
        "#define FLYNES_SESSION_SCHEMA_REGISTRY_HASH_HEX \""
        + digest
        + "\"\n\n"
        "#endif\n"
    ).encode("ascii")
    for dest in COPIES:
        dest.mkdir(parents=True, exist_ok=True)
        (dest / "flynes_session_v1.schema").write_bytes(schema_bytes)
        (dest / "schema_registry_hash.txt").write_bytes(hash_bytes)
    (CANONICAL / "schema_registry_hash.h").write_bytes(header)
    print(digest)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
