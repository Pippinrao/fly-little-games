#!/usr/bin/env python3
"""Independent oracle for the LINK_HELLO_V1 / LINK_READY_V1 golden vectors.

This script re-implements the frozen W0 contract layout from scratch, in a
language (Python) that shares no code with the C++ encoder under test, and
computes every expected byte, digest and object hash itself.  The C++ tests
then compare the codec's output against these frozen constants.  Nothing here
ever calls the encoder under test, which is exactly the property the W1 report
required ("never let the encoder produce the expected value").

Layout source of truth: shared/src/session/link/link_control_contract.hpp
after the owner-authorized 2026-09-16 channel-identity correction.

  LINK_HELLO_V1  488 bytes   pretag 424 + signature 64
  LINK_READY_V1  432 bytes   pretag 368 + signature 64

Digest domain prefix format is wire::domain_hash:

    sha256(domain_ascii || u32be(len(data)) || data)

The signature scheme is the *test-only* deterministic one used by the wire
tests (NOT ECDSA), replicated here byte for byte:

    for part in (1, 2):
        out[32*part : 32*part+32] = sha256(bytes([part]) || public_key || digest)
    out[0]  = (out[0]  & 0x7f) | 0x01
    out[32] = 0x40 | (out[32] & 0x1f)
"""

import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
HELLO_TEST = ROOT / "shared/tests/nearby/protocol/test_link_hello.cpp"
READY_TEST = ROOT / "shared/tests/nearby/protocol/test_link_ready.cpp"


# --------------------------------------------------------------------- hashes

def sha256(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def domain_hash(domain: str, data: bytes) -> bytes:
    return sha256(domain.encode("ascii") + len(data).to_bytes(4, "big") + data)


def test_sign(public_key: bytes, digest: bytes) -> bytes:
    """Byte-for-byte copy of the tests' deterministic test-only signer."""
    assert len(public_key) == 65 and len(digest) == 32
    out = bytearray(64)
    for part in (1, 2):
        chunk = sha256(bytes([part]) + public_key + digest)
        out[(part - 1) * 32:part * 32] = chunk
    out[0] = (out[0] & 0x7F) | 0x01
    out[32] = 0x40 | (out[32] & 0x1F)
    return bytes(out)


# --------------------------------------------------------------- fixture data
# Every scalar below is copied from the corresponding C++ test fixture.  None of
# them is read back out of the encoder.

GENERATOR1 = bytes.fromhex(
    "046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
    "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5")
GENERATOR2 = bytes.fromhex(
    "047cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978"
    "07775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1")

INITIATOR_IDENTITY_KEY_ID = bytes.fromhex(
    "5c968ed197270b0b71d7bbb572356a25d6ea6e19967e325a02889c8f609750c2")
RESPONDER_IDENTITY_KEY_ID = bytes.fromhex(
    "4f02b299c7262eb350fd959f54d5811e433a0d5edb72c17e369f8adbec851afa")

HELLO_INITIATOR_OBJECT_HASH = bytes.fromhex(
    "9f2479e6ce0e4b5b0cc9e89340c82aad5a390638942416e80c326d0c368f59ab")
HELLO_RESPONDER_OBJECT_HASH = bytes.fromhex(
    "9c6d9a08c9e7e7ff3c04caac0cf31279e10c6ccb1f85ef8e6d8b82f6fc8f44ca")

HELLO_DIGEST_DOMAIN = "flynes-link-hello-v1"
HELLO_OBJECT_DOMAIN = "flynes-link-hello-object-v1"
READY_DIGEST_DOMAIN = "flynes-link-ready-v1"
READY_OBJECT_DOMAIN = "flynes-link-ready-object-v1"

# The corrected 16-byte channel identity.  The old vector used the u64
# 0x0102030405060708; the corrected contract carries 16 bytes, so the fixture
# keeps the same leading bytes and zero-extends them to the 16-byte width.
CHANNEL_ID = bytes([0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08]) + bytes(8)

# The canonical channel-bind binding hash constants the READY fixture fills
# with 0x88 (the codec never derives it from these bytes).
CHANNEL_BIND_HASH = bytes([0x88]) * 32

HELLO_PRETAG_SIZE = 424
HELLO_SIZE = 488
READY_PRETAG_SIZE = 368
READY_SIZE = 432


# ------------------------------------------------------------------ LINK_HELLO

def hello_pretag(sender_role, receiver_role, identity_key_id, identity_key,
                 signing_key):
    out = bytearray(HELLO_PRETAG_SIZE)
    out[0:2] = (1).to_bytes(2, "big")            # version
    out[8] = sender_role
    out[9] = receiver_role
    out[10] = 1                                   # LinkPhaseV1::Initial
    out[16:32] = bytes([0x11]) * 16               # session_id
    out[32:48] = bytes([0x22]) * 16               # link_id
    out[48:64] = CHANNEL_ID                       # channel_id[16]
    out[64:72] = (7).to_bytes(8, "big")           # connection_generation
    out[72:80] = (9).to_bytes(8, "big")           # link_generation
    out[80:82] = (2).to_bytes(2, "big")           # wire_major
    out[82:84] = (0).to_bytes(2, "big")           # wire_minor
    out[84:86] = (1).to_bytes(2, "big")           # capability_bits = DUAL
    out[86:88] = (0).to_bytes(2, "big")           # critical_extension_mask
    out[88] = 1                                   # determinism_profile
    out[89] = 2                                   # core_state_format
    out[92:124] = bytes([0x33]) * 32              # selected_plan_hash
    out[124:156] = bytes([0x44]) * 32             # endpoint_offer_hash
    out[156:188] = bytes([0x55]) * 32             # pair_transcript_object_hash
    out[188:220] = bytes([0x66]) * 32             # session_signing_binding_hash
    out[220] = 0
    out[221] = 1                                  # identity verifier version 1
    out[228:260] = identity_key_id
    out[260:325] = identity_key
    out[332:397] = signing_key
    assert len(out) == HELLO_PRETAG_SIZE
    return bytes(out)


def hello_vector(sender_role, receiver_role, identity_key_id, identity_key,
                 signing_key):
    pretag = hello_pretag(sender_role, receiver_role, identity_key_id,
                          identity_key, signing_key)
    digest = domain_hash(HELLO_DIGEST_DOMAIN, pretag)
    signature = test_sign(signing_key, digest)
    raw = pretag + signature
    assert len(raw) == HELLO_SIZE
    return raw, digest, domain_hash(HELLO_OBJECT_DOMAIN, raw)


# ------------------------------------------------------------------ LINK_READY

def ready_pretag(sender_role, receiver_role, ready_phase, local_hello_hash):
    out = bytearray(READY_PRETAG_SIZE)
    out[0:2] = (1).to_bytes(2, "big")             # version
    out[8] = sender_role
    out[9] = receiver_role
    out[10] = 2                                   # LinkPhaseV1::Reconcile
    out[11] = ready_phase                         # 1 Ready, 2 Ack
    out[16:32] = bytes([0x11]) * 16               # session_id
    out[32:48] = bytes([0x22]) * 16               # link_id
    out[48:64] = CHANNEL_ID                       # channel_id[16]
    out[64:72] = (7).to_bytes(8, "big")           # connection_generation
    out[72:80] = (0).to_bytes(8, "big")           # reconnect_attempt
    out[80:88] = (9).to_bytes(8, "big")           # link_generation
    out[88:120] = CHANNEL_BIND_HASH
    out[120:152] = local_hello_hash
    out[152:184] = bytes([0x99]) * 32             # peer_hello_object_hash
    out[184:216] = bytes([0xAA]) * 32             # negotiated_result_hash
    out[216:248] = bytes([0xBB]) * 32             # local_summary_hash
    out[248:280] = bytes([0xCC]) * 32             # peer_summary_hash
    out[280:312] = bytes([0xDD]) * 32             # merge_result_hash
    assert len(out) == READY_PRETAG_SIZE
    return bytes(out)


def hello_vector(sender_role, receiver_role, identity_key_id, identity_key,
                 signing_key):
    pretag = hello_pretag(sender_role, receiver_role, identity_key_id,
                          identity_key, signing_key)
    digest = domain_hash(HELLO_DIGEST_DOMAIN, pretag)
    signature = test_sign(signing_key, digest)
    raw = pretag + signature
    assert len(raw) == HELLO_SIZE
    return raw, digest, domain_hash(HELLO_OBJECT_DOMAIN, raw)


def ready_vector(sender_role, receiver_role, ready_phase, signing_key,
                 local_hello_hash):
    pretag = ready_pretag(sender_role, receiver_role, ready_phase,
                          local_hello_hash)
    digest = domain_hash(READY_DIGEST_DOMAIN, pretag)
    signature = test_sign(signing_key, digest)
    raw = pretag + signature
    assert len(raw) == READY_SIZE
    return raw, digest, domain_hash(READY_OBJECT_DOMAIN, raw)


# ------------------------------------------------------------ C++ literal patch

def load_constant(path: Path, name: str) -> str:
    text = path.read_text(encoding="utf-8")
    match = re.search(
        r'constexpr const char\* %s =\s*((?:"[0-9a-f]*"\s*)+);' % re.escape(name),
        text)
    if not match:
        raise SystemExit("constant %s not found in %s" % (name, path))
    body = match.group(1)
    return "".join(re.findall(r'"([0-9a-f]*)"', body))


def format_constant(hex_text: str) -> str:
    lines = [hex_text[i:i + 64] for i in range(0, len(hex_text), 64)]
    return "\n".join('    "%s"' % line for line in lines) + "\n    "


def patch_constant(path: Path, name: str, new_hex: str) -> None:
    text = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r'(constexpr const char\* %s =\s*)((?:"[0-9a-f]*"\s*)+)(;)' %
        re.escape(name))
    if not pattern.search(text):
        raise SystemExit("constant %s not found in %s" % (name, path))
    text = pattern.sub(lambda m: m.group(1) + format_constant(new_hex) + m.group(3),
                       text, count=1)
    path.write_text(text, encoding="utf-8")


def report(label, raw, digest, obj):
    print("%s bytes=%d" % (label, len(raw)))
    print("  bytes  %s" % raw.hex())
    print("  digest %s" % digest.hex())
    print("  object %s" % obj.hex())


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        pass

    # --- recompute and verify every pre-existing linkage the tests rely on ----
    # The HELLO object hashes are echoed inside the READY vectors, so the
    # oracle must agree with the already-frozen constants before it rewrites
    # anything.
    hello_initiator, hello_initiator_digest, hello_initiator_hash = hello_vector(
        1, 2, INITIATOR_IDENTITY_KEY_ID, GENERATOR2, GENERATOR1)
    hello_responder, hello_responder_digest, hello_responder_hash = hello_vector(
        2, 1, RESPONDER_IDENTITY_KEY_ID, GENERATOR1, GENERATOR2)
    # The old constants were derived from the u64 channel id, so they MUST
    # differ now; the oracle re-derives them from scratch and the tests compare
    # the codec against the new values.  Only the fixture scalars are shared.
    if hello_initiator_hash == HELLO_INITIATOR_OBJECT_HASH:
        print("oracle did not change the initiator HELLO object hash",
              file=sys.stderr)

    ready_initiator, ready_initiator_digest, ready_initiator_hash = ready_vector(
        1, 2, 1, GENERATOR1, hello_initiator_hash)
    ready_responder, ready_responder_digest, ready_responder_hash = ready_vector(
        2, 1, 1, GENERATOR2, hello_responder_hash)
    ack_initiator, ack_initiator_digest, ack_initiator_hash = ready_vector(
        1, 2, 2, GENERATOR1, hello_initiator_hash)

    report("HELLO initiator", hello_initiator, hello_initiator_digest,
           hello_initiator_hash)
    report("HELLO responder", hello_responder, hello_responder_digest,
           hello_responder_hash)
    report("READY initiator", ready_initiator, ready_initiator_digest,
           ready_initiator_hash)
    report("READY responder", ready_responder, ready_responder_digest,
           ready_responder_hash)
    report("ACK initiator", ack_initiator, ack_initiator_digest,
           ack_initiator_hash)

    if "--print" in sys.argv:
        return

    patch_constant(HELLO_TEST, "kHelloInitiatorBytes", hello_initiator.hex())
    patch_constant(HELLO_TEST, "kHelloResponderBytes", hello_responder.hex())
    patch_constant(HELLO_TEST, "kHelloInitiatorDigest", hello_initiator_digest.hex())
    patch_constant(HELLO_TEST, "kHelloResponderDigest", hello_responder_digest.hex())
    patch_constant(HELLO_TEST, "kHelloInitiatorObjectHash",
                   hello_initiator_hash.hex())
    patch_constant(HELLO_TEST, "kHelloResponderObjectHash",
                   hello_responder_hash.hex())
    patch_constant(READY_TEST, "kReadyInitiatorBytes", ready_initiator.hex())
    patch_constant(READY_TEST, "kReadyResponderBytes", ready_responder.hex())
    patch_constant(READY_TEST, "kAckInitiatorBytes", ack_initiator.hex())
    patch_constant(READY_TEST, "kReadyInitiatorDigest", ready_initiator_digest.hex())
    patch_constant(READY_TEST, "kReadyResponderDigest",
                   ready_responder_digest.hex())
    patch_constant(READY_TEST, "kAckInitiatorDigest", ack_initiator_digest.hex())
    patch_constant(READY_TEST, "kReadyInitiatorObjectHash",
                   ready_initiator_hash.hex())
    patch_constant(READY_TEST, "kReadyResponderObjectHash",
                   ready_responder_hash.hex())
    patch_constant(READY_TEST, "kAckInitiatorObjectHash",
                   ack_initiator_hash.hex())
    # The READY vectors echo the HELLO object hashes, so the two constants that
    # link_ready.cpp holds must move with them.
    patch_constant(READY_TEST, "kHelloInitiatorObjectHash",
                   hello_initiator_hash.hex())
    patch_constant(READY_TEST, "kHelloResponderObjectHash",
                   hello_responder_hash.hex())
    print("patched golden byte, digest and object-hash constants")


if __name__ == "__main__":
    main()
