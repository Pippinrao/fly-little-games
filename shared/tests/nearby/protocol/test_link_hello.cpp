#include "wire/link_hello.hpp"
#include "wire/link_ready.hpp"
#include "wire/p256_point.hpp"
#include "wire/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace wire = flynes::session::wire;
namespace link = flynes::session::link;

int failures = 0;

void check(bool value, const char* message)
{
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

/* ------------------------------------------------------------------------- *
 * Golden constants. Produced once by an independent script (out/logs/
 * gen_golden.ps1) that re-implements the frozen contract layout from scratch;
 * they are hard-coded here on purpose so no expected value can ever come out of
 * the encoder under test.
 * ------------------------------------------------------------------------- */

// kInitiatorIdentityKeyId (32 bytes)
constexpr const char* kInitiatorIdentityKeyId =
    "5c968ed197270b0b71d7bbb572356a25d6ea6e19967e325a02889c8f609750c2"
    ;


// kResponderIdentityKeyId (32 bytes)
constexpr const char* kResponderIdentityKeyId =
    "4f02b299c7262eb350fd959f54d5811e433a0d5edb72c17e369f8adbec851afa"
    ;


// kHelloInitiatorBytes (480 bytes)
constexpr const char* kHelloInitiatorBytes =
    "0001000000000000010201000000000011111111111111111111111111111111"
    "2222222222222222222222222222222201020304050607080000000000000007"
    "0000000000000009000200000001000001020000333333333333333333333333"
    "3333333333333333333333333333333333333333444444444444444444444444"
    "4444444444444444444444444444444444444444555555555555555555555555"
    "5555555555555555555555555555555555555555666666666666666666666666"
    "666666666666666666666666666666666666666600010000000000005c968ed1"
    "97270b0b71d7bbb572356a25d6ea6e19967e325a02889c8f609750c2047cf27b"
    "188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978077755"
    "10db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1000000"
    "00000000046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a139"
    "45d898c2964fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb640"
    "6837bf51f5000000000000000000000000000000000000000000000000000000"
    "332f36f7f888dfccb1a9a363f6e6e1b284632e690d9ebb1f04e3756a58d05f08"
    "547a55af56926e0e6cede9bc721147d26355b2b151d9992a0843314b46475c89"
    ;


// kHelloInitiatorDigest (32 bytes)
constexpr const char* kHelloInitiatorDigest =
    "6f550877bcd9d63f869d221769349e16b05c2386b2dbff4391cc63e9ba73d8de"
    ;


// kHelloInitiatorObjectHash (32 bytes)
constexpr const char* kHelloInitiatorObjectHash =
    "9f2479e6ce0e4b5b0cc9e89340c82aad5a390638942416e80c326d0c368f59ab"
    ;


// kHelloResponderBytes (480 bytes)
constexpr const char* kHelloResponderBytes =
    "0001000000000000020101000000000011111111111111111111111111111111"
    "2222222222222222222222222222222201020304050607080000000000000007"
    "0000000000000009000200000001000001020000333333333333333333333333"
    "3333333333333333333333333333333333333333444444444444444444444444"
    "4444444444444444444444444444444444444444555555555555555555555555"
    "5555555555555555555555555555555555555555666666666666666666666666"
    "666666666666666666666666666666666666666600010000000000004f02b299"
    "c7262eb350fd959f54d5811e433a0d5edb72c17e369f8adbec851afa046b17d1"
    "f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c2964fe342"
    "e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5000000"
    "00000000047cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48"
    "fc4766997807775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b7"
    "9d227873d1000000000000000000000000000000000000000000000000000000"
    "11e0cf0aa78f0565b2e3f96d877119a59df8a86dfb1116f93fc246b8ee4fc8ae"
    "4ea8a842f1ae5aaf9bdd6a54a66b9c42cd7c6ee4d8cc5008b7a1adbc24698ff0"
    ;


// kHelloResponderDigest (32 bytes)
constexpr const char* kHelloResponderDigest =
    "9c6f969bc3d7458d510ce0966d5d22d1252049794489b6bfad57c53f2d6973b2"
    ;


// kHelloResponderObjectHash (32 bytes)
constexpr const char* kHelloResponderObjectHash =
    "9c6d9a08c9e7e7ff3c04caac0cf31279e10c6ccb1f85ef8e6d8b82f6fc8f44ca"
    ;

constexpr const char* kGenerator1 =
    "046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
    "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5";
constexpr const char* kGenerator2 =
    "047cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978"
    "07775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1";

std::uint8_t nibble(char value)
{
    return static_cast<std::uint8_t>(
        value <= '9' ? value - '0' : value - 'a' + 10);
}

template <std::size_t N>
std::array<std::uint8_t, N> hex_bytes(const char* hex)
{
    std::array<std::uint8_t, N> out{};
    for (std::size_t index = 0; index < N; ++index)
        out[index] = static_cast<std::uint8_t>(
            (nibble(hex[index * 2]) << 4u) | nibble(hex[index * 2 + 1]));
    return out;
}

/*
 * Test-only deterministic signature scheme. The verifier recomputes the same
 * value from the public key and the digest alone, so digest-domain separation,
 * double hashing and canonicality are all genuinely observable. It is NOT
 * ECDSA; production verification is a provider effect (see the report's
 * integration request). Every negative below is about what the codec feeds the
 * verifier and what it accepts back, which this scheme exercises exactly.
 */
std::array<std::uint8_t, 64> test_sign(
    const std::array<std::uint8_t, 65>& public_key,
    const std::array<std::uint8_t, 32>& digest)
{
    std::array<std::uint8_t, 64> out{};
    for (std::size_t part = 0; part < 2; ++part) {
        std::array<std::uint8_t, 1 + 65 + 32> buffer{};
        buffer[0] = static_cast<std::uint8_t>(part == 0 ? 1 : 2);
        std::copy(public_key.begin(), public_key.end(), buffer.begin() + 1);
        std::copy(digest.begin(), digest.end(), buffer.begin() + 66);
        const auto hash = wire::sha256(buffer.data(), buffer.size());
        std::copy(hash.begin(), hash.end(), out.begin() +
                                               static_cast<std::ptrdiff_t>(part * 32));
    }
    out[0] = static_cast<std::uint8_t>((out[0] & 0x7f) | 0x01);
    out[32] = static_cast<std::uint8_t>(0x40 | (out[32] & 0x1f));
    return out;
}

int verifier_calls = 0;

bool test_verify(void*, const std::uint8_t public_key[65],
                 const std::uint8_t digest[32],
                 const std::uint8_t signature[64])
{
    ++verifier_calls;
    std::array<std::uint8_t, 65> key{};
    std::copy_n(public_key, 65, key.begin());
    std::array<std::uint8_t, 32> message{};
    std::copy_n(digest, 32, message.begin());
    const auto expected = test_sign(key, message);
    return std::memcmp(expected.data(), signature, 64) == 0;
}

/* ------------------------------------------------------------- fixtures */

link::LinkHelloV1 golden_hello_value()
{
    link::LinkHelloV1 value{};
    value.version = 1;
    value.sender_role = wire::PairRoleV1::Initiator;
    value.receiver_role = wire::PairRoleV1::Responder;
    value.phase = link::LinkPhaseV1::Initial;
    value.session_id.fill(0x11);
    value.link_id.fill(0x22);
    value.channel_id = 0x0102030405060708ull;
    value.connection_generation = 7;
    value.link_generation = 9;
    value.wire_major = 2;
    value.wire_minor = 0;
    value.capability_bits = link::kLinkSupportedCapabilityMaskV1;
    value.critical_extension_mask = 0;
    value.determinism_profile = 1;
    value.core_state_format = 2;
    value.selected_plan_hash.fill(0x33);
    value.endpoint_offer_hash.fill(0x44);
    value.pair_transcript_object_hash.fill(0x55);
    value.session_signing_binding_hash.fill(0x66);
    value.identity_verifier_ref.version = {{0, 1}};
    value.identity_verifier_ref.identity_key_id =
        hex_bytes<32>(kInitiatorIdentityKeyId);
    value.identity_verifier_ref.identity_public_key = hex_bytes<65>(kGenerator2);
    value.session_signing_public_key = hex_bytes<65>(kGenerator1);
    return value;
}

wire::LinkHelloExpectationsV1 hello_expectations()
{
    wire::LinkHelloExpectationsV1 expected{};
    expected.session_id.fill(0x11);
    expected.link_id.fill(0x22);
    expected.channel_id = 0x0102030405060708ull;
    expected.connection_generation = 7;
    expected.link_generation = 9;
    expected.local_role = wire::PairRoleV1::Responder;
    expected.pair_transcript_object_hash.fill(0x55);
    expected.selected_plan_hash.fill(0x33);
    expected.endpoint_offer_hash.fill(0x44);
    expected.peer_binding_hash.fill(0x66);
    expected.peer_identity_key_id = hex_bytes<32>(kInitiatorIdentityKeyId);
    expected.peer_session_signing_public_key = hex_bytes<65>(kGenerator1);
    expected.peer_identity_public_key = hex_bytes<65>(kGenerator2);
    return expected;
}

struct HelloDecode
{
    wire::Status status = wire::Status::Ok;
    wire::LinkControlIssueV1 issue = wire::LinkControlIssueV1::None;
    link::LinkProposalSupportV1 proposal = link::LinkProposalSupportV1::Supported;
    link::LinkHelloV1 value{};
};

HelloDecode decode_hello(const std::vector<std::uint8_t>& bytes,
                         const wire::LinkHelloExpectationsV1& expected)
{
    HelloDecode out{};
    wire::LinkControlDecodeReportV1 report{};
    out.status = wire::decode_link_hello_v1(
        bytes.data(), bytes.size(), expected,
        wire::validate_p256_uncompressed_point_callback, nullptr, test_verify,
        nullptr, &report, &out.value);
    out.issue = report.issue;
    out.proposal = report.proposal;
    return out;
}

std::vector<std::uint8_t> golden_initiator_hello_bytes()
{
    const auto golden = hex_bytes<480>(kHelloInitiatorBytes);
    return std::vector<std::uint8_t>(golden.begin(), golden.end());
}

/* ------------------------------------------------------------- positives */

void golden_initiator_hello()
{
    const auto expected_bytes = hex_bytes<480>(kHelloInitiatorBytes);
    const auto expected_digest = hex_bytes<32>(kHelloInitiatorDigest);
    const auto expected_object_hash = hex_bytes<32>(kHelloInitiatorObjectHash);

    auto value = golden_hello_value();
    std::array<std::uint8_t, link::kLinkHelloPretagSizeV1> pretag{};
    std::array<std::uint8_t, 32> digest{};
    check(wire::build_link_hello_pretag_v1(
              value, wire::validate_p256_uncompressed_point_callback, nullptr,
              &pretag, &digest) == wire::Status::Ok,
          "initiator HELLO pretag encodes");

    const std::array<std::uint8_t, link::kLinkHelloPretagSizeV1> expected_pretag =
        [&expected_bytes] {
            std::array<std::uint8_t, link::kLinkHelloPretagSizeV1> out{};
            std::copy_n(expected_bytes.begin(),
                        link::kLinkHelloPretagSizeV1, out.begin());
            return out;
        }();
    check(pretag == expected_pretag,
          "initiator HELLO pretag equals the independently generated golden bytes");
    check(digest == expected_digest,
          "initiator HELLO digest equals the independent golden digest");

    const auto signature = test_sign(value.session_signing_public_key,
                                     expected_digest);
    std::array<std::uint8_t, link::kLinkHelloSizeV1> bytes{};
    link::LinkHelloV1 parsed{};
    check(wire::finish_link_hello_v1(pretag, signature, &bytes, &parsed) ==
              wire::Status::Ok,
          "initiator HELLO finishes with a canonical signature");
    check(bytes == expected_bytes,
          "initiator HELLO is byte-exact against the independent golden vector");
    check(parsed.object_hash == expected_object_hash,
          "initiator HELLO object hash equals the independent golden hash");
    check(parsed.digest == expected_digest,
          "finish reports the digest of the signed pretag");

    verifier_calls = 0;
    auto decoded = decode_hello(golden_initiator_hello_bytes(),
                                hello_expectations());
    check(decoded.status == wire::Status::Ok,
          "initiator HELLO decodes with matching expectations");
    check(verifier_calls == 1,
          "initiator HELLO verification ran exactly once");
    check(decoded.value.sender_role == wire::PairRoleV1::Initiator &&
              decoded.value.receiver_role == wire::PairRoleV1::Responder &&
              decoded.value.phase == link::LinkPhaseV1::Initial,
          "decoded initiator HELLO carries the signed roles and phase");
    check(decoded.value.object_hash == expected_object_hash,
          "decoded initiator HELLO regenerates the persisted object hash");
    check(decoded.value.session_signing_public_key ==
              hex_bytes<65>(kGenerator1),
          "decoded initiator HELLO binds the sender session signing key");
}

void golden_responder_hello()
{
    const auto expected_bytes = hex_bytes<480>(kHelloResponderBytes);
    const auto expected_digest = hex_bytes<32>(kHelloResponderDigest);
    const auto expected_object_hash = hex_bytes<32>(kHelloResponderObjectHash);

    link::LinkHelloV1 value{};
    value.sender_role = wire::PairRoleV1::Responder;
    value.receiver_role = wire::PairRoleV1::Initiator;
    value.session_id.fill(0x11);
    value.link_id.fill(0x22);
    value.channel_id = 0x0102030405060708ull;
    value.connection_generation = 7;
    value.link_generation = 9;
    value.determinism_profile = 1;
    value.core_state_format = 2;
    value.selected_plan_hash.fill(0x33);
    value.endpoint_offer_hash.fill(0x44);
    value.pair_transcript_object_hash.fill(0x55);
    value.session_signing_binding_hash.fill(0x66);
    value.identity_verifier_ref.version = {{0, 1}};
    value.identity_verifier_ref.identity_key_id =
        hex_bytes<32>(kResponderIdentityKeyId);
    value.identity_verifier_ref.identity_public_key = hex_bytes<65>(kGenerator1);
    value.session_signing_public_key = hex_bytes<65>(kGenerator2);

    std::array<std::uint8_t, link::kLinkHelloPretagSizeV1> pretag{};
    std::array<std::uint8_t, 32> digest{};
    check(wire::build_link_hello_pretag_v1(
              value, wire::validate_p256_uncompressed_point_callback, nullptr,
              &pretag, &digest) == wire::Status::Ok,
          "responder HELLO pretag encodes");
    check(digest == expected_digest,
          "responder HELLO digest equals the independent golden digest");

    const auto signature =
        test_sign(value.session_signing_public_key, expected_digest);
    std::array<std::uint8_t, link::kLinkHelloSizeV1> bytes{};
    link::LinkHelloV1 parsed{};
    check(wire::finish_link_hello_v1(pretag, signature, &bytes, &parsed) ==
              wire::Status::Ok,
          "responder HELLO finishes");
    check(bytes == expected_bytes,
          "responder HELLO is byte-exact against the independent golden vector");
    check(parsed.object_hash == expected_object_hash,
          "responder HELLO object hash equals the independent golden hash");

    auto expected = hello_expectations();
    expected.local_role = wire::PairRoleV1::Initiator;
    expected.peer_identity_key_id = hex_bytes<32>(kResponderIdentityKeyId);
    expected.peer_session_signing_public_key = hex_bytes<65>(kGenerator2);
    expected.peer_identity_public_key = hex_bytes<65>(kGenerator1);
    const std::vector<std::uint8_t> raw(bytes.begin(), bytes.end());
    const auto decoded = decode_hello(raw, expected);
    check(decoded.status == wire::Status::Ok,
          "responder HELLO decodes for the mirrored roles");
}

void exact_field_offsets()
{
    const auto bytes = golden_initiator_hello_bytes();
    check(bytes[0] == 0x00 && bytes[1] == 0x01,
          "HELLO version is u16be 1 at offset 0");
    check(bytes[8] == 1 && bytes[9] == 2 && bytes[10] == 1,
          "HELLO roles and phase sit at offsets 8/9/10");
    check(bytes[16] == 0x11 && bytes[32] == 0x22,
          "HELLO session and link ids sit at offsets 16/32");
    check(bytes[48] == 0x01 && bytes[55] == 0x08,
          "HELLO channel_id is u64be at offset 48");
    check(bytes[56] == 0x00 && bytes[63] == 0x07,
          "HELLO connection_generation is u64be at offset 56");
    check(bytes[64] == 0x00 && bytes[71] == 0x09,
          "HELLO link_generation is u64be at offset 64");
    check(bytes[72] == 0x00 && bytes[73] == 0x02 && bytes[74] == 0x00 &&
              bytes[75] == 0x00,
          "HELLO wire version 2/0 sits at offsets 72..76");
    check(bytes[76] == 0x00 && bytes[77] == 0x01,
          "HELLO capability bits carry DUAL at offset 76");
    check(bytes[78] == 0 && bytes[79] == 0,
          "HELLO critical_extension_mask is zero at offset 78");
    check(bytes[80] == 1 && bytes[81] == 2,
          "HELLO determinism/core format sit at offsets 80/81");
    check(bytes[84] == 0x33 && bytes[116] == 0x44 && bytes[148] == 0x55 &&
              bytes[180] == 0x66,
          "HELLO plan/offer/transcript/binding hashes are contiguous");
    check(bytes[212] == 0x00 && bytes[213] == 0x01,
          "HELLO identity verifier ref version is u16be 1 at offset 212");
    check(bytes[324] == 0x04,
          "HELLO session signing public key starts at offset 324");
    check(bytes[416] != 0 && bytes[479] != 0,
          "HELLO signature occupies offsets 416..480");
    check(std::all_of(bytes.begin() + 389, bytes.begin() + 416,
                      [](std::uint8_t value) { return value == 0; }),
          "HELLO reserved tail before the signature is zeroed");
}

void stream_is_never_advertised()
{
    auto value = golden_hello_value();
    value.capability_bits = static_cast<std::uint16_t>(
        link::kLinkCapabilityStreamVideoV1 | link::kLinkCapabilityStreamAudioV1);
    std::array<std::uint8_t, link::kLinkHelloPretagSizeV1> pretag{};
    std::array<std::uint8_t, 32> digest{};
    check(wire::build_link_hello_pretag_v1(
              value, wire::validate_p256_uncompressed_point_callback, nullptr,
              &pretag, &digest) == wire::Status::InvalidField,
          "encoder refuses to advertise a STREAM-only capability set");

    value.capability_bits = static_cast<std::uint16_t>(
        link::kLinkSupportedCapabilityMaskV1 | link::kLinkCapabilityStreamVideoV1);
    check(wire::build_link_hello_pretag_v1(
              value, wire::validate_p256_uncompressed_point_callback, nullptr,
              &pretag, &digest) == wire::Status::InvalidField,
          "encoder refuses to advertise DUAL plus STREAM");

    value.capability_bits = link::kLinkSupportedCapabilityMaskV1;
    value.critical_extension_mask = 1;
    check(wire::build_link_hello_pretag_v1(
              value, wire::validate_p256_uncompressed_point_callback, nullptr,
              &pretag, &digest) == wire::Status::UnknownCriticalTag,
          "encoder refuses a nonzero critical extension mask");
}

void peer_capability_gate()
{
    /* The encoder never advertises STREAM, so every peer-side capability
     * fixture below is produced by rewriting the capability field of a legal
     * pretag and re-signing it, which is exactly what a hostile peer does. */
    auto value = golden_hello_value();
    std::array<std::uint8_t, link::kLinkHelloPretagSizeV1> peer_pretag{};
    std::array<std::uint8_t, 32> peer_digest{};
    check(wire::build_link_hello_pretag_v1(
              value, wire::validate_p256_uncompressed_point_callback, nullptr,
              &peer_pretag, &peer_digest) == wire::Status::Ok,
          "base pretag for the peer capability fixtures encodes");

    std::array<std::uint8_t, link::kLinkHelloSizeV1> peer_bytes{};
    link::LinkHelloV1 peer_parsed{};

    /* DUAL plus a known STREAM bit is tolerable: the peer merely advertises more
     * than this release uses, so it is accepted as supported. */
    peer_pretag[76] = 0x00;
    peer_pretag[77] = 0x03;
    peer_digest = wire::domain_hash(link::kLinkHelloDigestDomainV1,
                                    peer_pretag.data(), peer_pretag.size());
    check(wire::finish_link_hello_v1(
              peer_pretag,
              test_sign(value.session_signing_public_key, peer_digest),
              &peer_bytes, &peer_parsed) == wire::Status::Ok,
          "DUAL plus a known STREAM bit peer HELLO is signed");
    const std::vector<std::uint8_t> dual_stream_raw(peer_bytes.begin(),
                                                    peer_bytes.end());
    verifier_calls = 0;
    auto decoded = decode_hello(dual_stream_raw, hello_expectations());
    check(decoded.status == wire::Status::Ok &&
              decoded.proposal == link::LinkProposalSupportV1::Supported &&
              verifier_calls == 1,
          "DUAL plus a known STREAM bit is accepted as supported");

    /* A STREAM-only proposal is explicitly unsupported: it must never be
     * silently degraded to DUAL and must never half-connect. */
    peer_pretag[76] = 0x00;
    peer_pretag[77] = 0x06;
    peer_digest = wire::domain_hash(link::kLinkHelloDigestDomainV1,
                                    peer_pretag.data(), peer_pretag.size());
    check(wire::finish_link_hello_v1(
              peer_pretag,
              test_sign(value.session_signing_public_key, peer_digest),
              &peer_bytes, &peer_parsed) == wire::Status::Ok,
          "STREAM-only peer HELLO is signed");
    const std::vector<std::uint8_t> stream_raw(peer_bytes.begin(),
                                               peer_bytes.end());
    verifier_calls = 0;
    const auto stream_decoded = decode_hello(stream_raw, hello_expectations());
    check(stream_decoded.status == wire::Status::InvalidField &&
              stream_decoded.issue == wire::LinkControlIssueV1::Capability &&
              stream_decoded.proposal ==
                  link::LinkProposalSupportV1::UnsupportedByThisRelease,
          "STREAM-only peer HELLO is rejected as explicitly unsupported");
    check(verifier_calls == 0,
          "unsupported capability is rejected before any signature check");

    /* An empty capability offer is likewise explicitly unsupported. */
    peer_pretag[76] = 0x00;
    peer_pretag[77] = 0x00;
    peer_digest = wire::domain_hash(link::kLinkHelloDigestDomainV1,
                                    peer_pretag.data(), peer_pretag.size());
    check(wire::finish_link_hello_v1(
              peer_pretag,
              test_sign(value.session_signing_public_key, peer_digest),
              &peer_bytes, &peer_parsed) == wire::Status::Ok,
          "empty-capability peer HELLO is signed");
    const std::vector<std::uint8_t> empty_raw(peer_bytes.begin(),
                                              peer_bytes.end());
    const auto empty_decoded = decode_hello(empty_raw, hello_expectations());
    check(empty_decoded.status == wire::Status::InvalidField &&
              empty_decoded.proposal ==
                  link::LinkProposalSupportV1::UnsupportedByThisRelease,
          "an empty capability offer is unsupported, not degraded");

    /* Unknown critical capability bits fail closed even with DUAL present. */
    peer_pretag[76] = 0x01;
    peer_pretag[77] = 0x01;
    peer_digest = wire::domain_hash(link::kLinkHelloDigestDomainV1,
                                    peer_pretag.data(), peer_pretag.size());
    check(wire::finish_link_hello_v1(
              peer_pretag,
              test_sign(value.session_signing_public_key, peer_digest),
              &peer_bytes, &peer_parsed) == wire::Status::Ok,
          "unknown capability HELLO is signed");
    const std::vector<std::uint8_t> unknown_raw(peer_bytes.begin(),
                                                peer_bytes.end());
    const auto unknown_decoded = decode_hello(unknown_raw, hello_expectations());
    check(unknown_decoded.status == wire::Status::UnknownCriticalTag &&
              unknown_decoded.issue == wire::LinkControlIssueV1::Capability &&
              unknown_decoded.proposal ==
                  link::LinkProposalSupportV1::UnknownCriticalCapability,
          "unknown critical capability bits fail closed");
}

/* ------------------------------------------------------------- negatives */

void structural_negatives()
{
    const auto expected = hello_expectations();
    const auto golden = golden_initiator_hello_bytes();

    auto truncated = golden;
    truncated.pop_back();
    check(decode_hello(truncated, expected).status == wire::Status::Truncated,
          "HELLO rejects truncation");

    auto trailing = golden;
    trailing.push_back(0);
    check(decode_hello(trailing, expected).status == wire::Status::Trailing,
          "HELLO rejects trailing bytes");

    struct Mutation
    {
        std::size_t offset;
        std::uint8_t value;
        const char* message;
    };
    const Mutation reserved[] = {
        {3, 1, "HELLO rejects nonzero reserved byte 3"},
        {11, 1, "HELLO rejects nonzero reserved byte 11"},
        {82, 1, "HELLO rejects nonzero reserved byte 82"},
        {400, 1, "HELLO rejects nonzero reserved tail byte"},
        {218, 1, "HELLO rejects nonzero identity ref reserved bytes"},
        {390, 1, "HELLO rejects nonzero identity ref trailing reserved bytes"}};
    for (const auto& mutation : reserved) {
        auto bytes = golden;
        bytes[mutation.offset] = mutation.value;
        const auto decoded = decode_hello(bytes, expected);
        check(decoded.status == wire::Status::NonzeroReserved &&
                  decoded.issue == wire::LinkControlIssueV1::Reserved,
              mutation.message);
    }

    auto version = golden;
    version[1] = 2;
    check(decode_hello(version, expected).status == wire::Status::UnknownEnum,
          "HELLO rejects an unknown object version");

    auto sender = golden;
    sender[8] = 3;
    check(decode_hello(sender, expected).status == wire::Status::UnknownEnum,
          "HELLO rejects an unknown sender role");

    auto receiver = golden;
    receiver[9] = 0;
    check(decode_hello(receiver, expected).status == wire::Status::UnknownEnum,
          "HELLO rejects an unknown receiver role");

    auto reflected = golden;
    reflected[8] = 1;
    reflected[9] = 1;
    check(decode_hello(reflected, expected).status == wire::Status::InvalidField,
          "HELLO rejects two identical (unmirrored) roles");

    auto reversed = golden;
    reversed[8] = 2;
    reversed[9] = 1;
    check(decode_hello(reversed, expected).status == wire::Status::InvalidField,
          "HELLO rejects a reflected role assignment");

    auto phase = golden;
    phase[10] = 2;
    check(decode_hello(phase, expected).status == wire::Status::InvalidField,
          "HELLO rejects phase RECONCILE");
    phase[10] = 9;
    check(decode_hello(phase, expected).status == wire::Status::UnknownEnum,
          "HELLO rejects an unknown phase");

    auto wire_version = golden;
    wire_version[73] = 3;
    check(decode_hello(wire_version, expected).status == wire::Status::InvalidField,
          "HELLO rejects a different wire major version");
}

void identity_and_binding_negatives()
{
    const auto golden = golden_initiator_hello_bytes();

    struct Mutation
    {
        std::size_t offset;
        std::uint8_t value;
        wire::LinkControlIssueV1 issue;
        const char* message;
    };
    const Mutation mismatches[] = {
        {16, 0x99, wire::LinkControlIssueV1::ExpectedField,
         "HELLO rejects a different session id"},
        {32, 0x99, wire::LinkControlIssueV1::ExpectedField,
         "HELLO rejects a different link id"},
        {55, 0x99, wire::LinkControlIssueV1::ExpectedField,
         "HELLO rejects a different channel id"},
        {63, 0x08, wire::LinkControlIssueV1::Generation,
         "HELLO rejects a different connection generation"},
        {71, 0x0a, wire::LinkControlIssueV1::Generation,
         "HELLO rejects a different link generation"},
        {148, 0x99, wire::LinkControlIssueV1::ExpectedField,
         "HELLO rejects a different pair transcript object hash"},
        {180, 0x99, wire::LinkControlIssueV1::BindingRef,
         "HELLO rejects a different session signing binding hash"},
        {220, 0x99, wire::LinkControlIssueV1::IdentityRef,
         "HELLO rejects a different identity key id"},
        {324, 0x99, wire::LinkControlIssueV1::BindingRef,
         "HELLO rejects a session signing key the binding never authenticated"},
        {84, 0x00, wire::LinkControlIssueV1::ExpectedField,
         "HELLO rejects a zeroed locked plan hash"}};
    const auto expected = hello_expectations();
    for (const auto& mutation : mismatches) {
        auto bytes = golden;
        bytes[mutation.offset] = mutation.value;
        const auto decoded = decode_hello(bytes, expected);
        check(decoded.status == wire::Status::InvalidField &&
                  decoded.issue == mutation.issue,
              mutation.message);
    }

    /* Substituting a different (but valid) long-term identity key inside
     * identity_verifier_ref must fail, since the HELLO must be authenticated by
     * the identity the accepted pair transcript pinned. */
    auto identity = golden;
    const auto other_identity = hex_bytes<65>(kGenerator1);
    std::copy(other_identity.begin(), other_identity.end(),
              identity.begin() + 252);
    const auto decoded_identity = decode_hello(identity, expected);
    check(decoded_identity.status == wire::Status::InvalidField &&
              decoded_identity.issue == wire::LinkControlIssueV1::IdentityRef,
          "HELLO rejects a substituted long-term identity key");

    /* A HELLO whose identity key id is consistent with a *different* identity
     * key than the accepted one is still rejected. */
    auto swapped = golden;
    const auto other_key_id = hex_bytes<32>(kResponderIdentityKeyId);
    std::copy(other_key_id.begin(), other_key_id.end(), swapped.begin() + 220);
    const auto decoded_swapped = decode_hello(swapped, expected);
    check(decoded_swapped.status == wire::Status::InvalidField &&
              decoded_swapped.issue == wire::LinkControlIssueV1::IdentityRef,
          "HELLO rejects an identity key id from another peer");

    /* Mutating the identity key id alone (inconsistent with the embedded long
     * term key) is also rejected. */
    auto inconsistent = golden;
    inconsistent[220] = 0x77;
    const auto decoded_inconsistent = decode_hello(inconsistent, expected);
    check(decoded_inconsistent.status == wire::Status::InvalidField &&
              decoded_inconsistent.issue == wire::LinkControlIssueV1::IdentityRef,
          "HELLO rejects an identity key id that does not match its own key");
}

void signature_negatives()
{
    const auto golden = golden_initiator_hello_bytes();
    const auto expected = hello_expectations();
    const auto digest = hex_bytes<32>(kHelloInitiatorDigest);

    auto zeroed = golden;
    std::fill(zeroed.begin() + 416, zeroed.end(), std::uint8_t{0});
    verifier_calls = 0;
    auto decoded = decode_hello(zeroed, expected);
    check(decoded.status == wire::Status::InvalidField &&
              decoded.issue == wire::LinkControlIssueV1::Signature &&
              verifier_calls == 0,
          "HELLO rejects a zeroed signature without calling the verifier");

    /* Non-canonical high-S: the same mathematical signature is re-encoded with
     * s replaced by its complement. The codec must refuse it as a form error
     * before any verification happens. */
    auto high_s = golden;
    high_s[448] = 0x80;
    verifier_calls = 0;
    decoded = decode_hello(high_s, expected);
    check(decoded.status == wire::Status::InvalidField &&
              decoded.issue == wire::LinkControlIssueV1::Signature &&
              verifier_calls == 0,
          "HELLO rejects a non-canonical high-S signature before verifying");

    /* Double hashing: signing sha256(digest) instead of the digest. */
    auto double_hashed = golden;
    {
        const auto inner = wire::sha256(digest.data(), digest.size());
        const auto bad = test_sign(hex_bytes<65>(kGenerator1), inner);
        std::copy(bad.begin(), bad.end(), double_hashed.begin() + 416);
    }
    verifier_calls = 0;
    decoded = decode_hello(double_hashed, expected);
    check(decoded.status == wire::Status::InvalidField &&
              decoded.issue == wire::LinkControlIssueV1::Signature &&
              verifier_calls == 1,
          "HELLO rejects a double-hashed signature");

    /* Wrong purpose: the digest of the READY domain over the same pretag. */
    auto wrong_purpose = golden;
    {
        const auto ready_domain_digest = wire::domain_hash(
            link::kLinkReadyDigestDomainV1, golden.data(),
            link::kLinkHelloPretagSizeV1);
        const auto bad =
            test_sign(hex_bytes<65>(kGenerator1), ready_domain_digest);
        std::copy(bad.begin(), bad.end(), wrong_purpose.begin() + 416);
    }
    decoded = decode_hello(wrong_purpose, expected);
    check(decoded.status == wire::Status::InvalidField &&
              decoded.issue == wire::LinkControlIssueV1::Signature,
          "HELLO rejects a signature made under the READY domain");

    /* The peer's own signature is not transferable to the other session key. */
    auto wrong_key = golden;
    {
        const auto bad = test_sign(hex_bytes<65>(kGenerator2), digest);
        std::copy(bad.begin(), bad.end(), wrong_key.begin() + 416);
    }
    decoded = decode_hello(wrong_key, expected);
    check(decoded.status == wire::Status::InvalidField &&
              decoded.issue == wire::LinkControlIssueV1::Signature,
          "HELLO rejects a signature that does not belong to the bound session key");

    /* A flipped payload bit invalidates the signature. */
    auto flipped = golden;
    flipped[90] ^= 0x01;
    decoded = decode_hello(flipped, expected);
    check(decoded.status == wire::Status::InvalidField,
          "HELLO rejects a payload bit flipped after signing");

    /* Swapped roles re-signed by the sender are still rejected: the digest is
     * authentic, the role assignment is not. */
    auto swapped_roles = golden;
    swapped_roles[8] = 2;
    swapped_roles[9] = 1;
    {
        const auto swapped_digest = wire::domain_hash(
            link::kLinkHelloDigestDomainV1, swapped_roles.data(),
            link::kLinkHelloPretagSizeV1);
        const auto bad = test_sign(hex_bytes<65>(kGenerator1), swapped_digest);
        std::copy(bad.begin(), bad.end(), swapped_roles.begin() + 416);
    }
    verifier_calls = 0;
    decoded = decode_hello(swapped_roles, expected);
    check(decoded.status == wire::Status::InvalidField &&
              decoded.issue == wire::LinkControlIssueV1::Discriminant &&
              verifier_calls == 0,
          "HELLO rejects a correctly signed but role-reflected message");

    /* Fail closed with no verifier at all. */
    wire::LinkControlDecodeReportV1 report{};
    link::LinkHelloV1 value{};
    check(wire::decode_link_hello_v1(
              golden.data(), golden.size(), expected,
              wire::validate_p256_uncompressed_point_callback, nullptr, nullptr,
              nullptr, &report, &value) == wire::Status::InvalidField,
          "HELLO refuses to decode without a signature verifier");
}

void wrong_kind_and_constants()
{
    const auto hello = golden_initiator_hello_bytes();
    wire::LinkControlDecodeReportV1 report{};
    link::LinkReadyV1 ready_value{};
    wire::LinkReadyExpectationsV1 ready_expected{};
    check(wire::decode_link_ready_v1(
              hello.data(), hello.size(), ready_expected,
              wire::validate_p256_uncompressed_point_callback, nullptr,
              test_verify, nullptr, &report, &ready_value) ==
              wire::Status::Trailing,
          "LINK_HELLO bytes are not accepted as a LINK_READY object");
    check(link::kLinkHelloObjectKindV1 == 0x0216 &&
              link::kLinkReadyObjectKindV1 == 0x0217 &&
              link::kLinkHelloObjectKindV1 != link::kLinkReadyObjectKindV1,
          "HELLO and READY object kinds differ");
    check(link::kLinkHelloMessageTagV1 == 0xFF06 &&
              link::kLinkReadyMessageTagV1 == 0xFF07,
          "HELLO and READY message tags differ");
    check(std::strcmp(link::kLinkHelloDigestDomainV1, "flynes-link-hello-v1") ==
              0,
          "HELLO digest domain comes from the frozen contract");
    check(std::strcmp(link::kLinkHelloObjectHashDomainV1,
                      "flynes-link-hello-object-v1") == 0,
          "HELLO object hash domain comes from the frozen contract");
}

/*
 * The two-stage contract. Stage 1 parses and validates every non-cryptographic
 * field and hands back the exact verification request; stage 2 accepts or fails
 * closed once the asynchronous verification result exists. The signature
 * rejection point therefore moved out of the decoder, without weakening any
 * negative case.
 */
void two_stage_parse_then_accept()
{
    auto expected = hello_expectations();
    const auto golden = golden_initiator_hello_bytes();

    wire::LinkControlDecodeReportV1 report{};
    wire::LinkControlParsedV1 parsed{};
    link::LinkHelloV1 value{};
    check(wire::parse_link_hello_v1(
              golden.data(), golden.size(), expected,
              wire::validate_p256_uncompressed_point_callback, nullptr, &report,
              &parsed, &value) == wire::Status::Ok,
          "stage 1 parses a well-formed HELLO without any cryptography");

    const auto expected_digest = hex_bytes<32>(kHelloInitiatorDigest);
    const auto expected_object_hash = hex_bytes<32>(kHelloInitiatorObjectHash);
    check(parsed.digest == expected_digest &&
              value.digest == expected_digest &&
              value.object_hash == expected_object_hash,
          "stage 1 already fixes the exact digest and object hash");

    wire::LinkControlVerifyRequestV1 request{};
    const std::string digest_domain = link::kLinkHelloDigestDomainV1;
    check(wire::link_control_verify_request_v1(
              parsed, link::kLinkHelloDigestDomainV1, &request) ==
              wire::Status::Ok &&
              request.public_key_x963 == hex_bytes<65>(kGenerator1) &&
              request.digest == expected_digest &&
              request.signature == parsed.signature &&
              request.domain.size() == digest_domain.size() &&
              std::equal(request.domain.begin(), request.domain.end(),
                         digest_domain.begin()),
          "stage 1 produces the exact asynchronous verification request");

    /* Accepted only once the verifier has answered. */
    link::LinkHelloV1 accepted{};
    check(wire::accept_link_hello_v1(
              parsed, value, wire::LinkControlVerificationOutcomeV1::Accepted,
              &report, &accepted) == wire::Status::Ok &&
              accepted.object_hash == expected_object_hash,
          "stage 2 accepts after a successful asynchronous verification");

    /* A rejected signature is the only thing stage 2 may reject. */
    link::LinkHelloV1 rejected{};
    report = wire::LinkControlDecodeReportV1{};
    check(wire::accept_link_hello_v1(
              parsed, value, wire::LinkControlVerificationOutcomeV1::Rejected,
              &report, &rejected) == wire::Status::InvalidField &&
              report.issue == wire::LinkControlIssueV1::Signature,
          "stage 2 fails closed when the asynchronous verifier rejected");

    /*
     * A structurally valid but wrong signature is now accepted by stage 1 and
     * rejected only by stage 2: this is the moved rejection point, and the
     * message is never persisted or answered.
     */
    auto tampered = golden;
    tampered[420] = static_cast<std::uint8_t>(tampered[420] ^ 0x01u);
    wire::LinkControlDecodeReportV1 tampered_report{};
    wire::LinkControlParsedV1 tampered_parsed{};
    link::LinkHelloV1 tampered_value{};
    check(wire::parse_link_hello_v1(
              tampered.data(), tampered.size(), expected,
              wire::validate_p256_uncompressed_point_callback, nullptr,
              &tampered_report, &tampered_parsed, &tampered_value) ==
              wire::Status::Ok,
          "stage 1 defers the signature decision to stage 2");
    wire::LinkControlDecodeReportV1 tampered_accept_report{};
    link::LinkHelloV1 tampered_accepted{};
    check(wire::accept_link_hello_v1(
              tampered_parsed, tampered_value,
              wire::LinkControlVerificationOutcomeV1::Rejected,
              &tampered_accept_report, &tampered_accepted) ==
              wire::Status::InvalidField &&
              tampered_accept_report.issue == wire::LinkControlIssueV1::Signature,
          "a tampered signature is rejected only after the async result returns");

    /*
     * The non-cryptographic negatives are unchanged and now land in stage 1, so
     * no verifier is ever consulted for a message that is structurally illegal.
     */
    auto reflected = golden;
    reflected[8] = 2;
    reflected[9] = 1;
    wire::LinkControlDecodeReportV1 reflected_report{};
    wire::LinkControlParsedV1 reflected_parsed{};
    link::LinkHelloV1 reflected_value{};
    check(wire::parse_link_hello_v1(
              reflected.data(), reflected.size(), expected,
              wire::validate_p256_uncompressed_point_callback, nullptr,
              &reflected_report, &reflected_parsed, &reflected_value) ==
              wire::Status::InvalidField &&
              reflected_report.issue == wire::LinkControlIssueV1::Discriminant,
          "stage 1 still rejects a role-reflected HELLO before any crypto");

    /* A non-canonical (zero) signature is still a stage-1 rejection. */
    auto zero_signature = golden;
    std::fill(zero_signature.begin() + 416, zero_signature.end(), 0);
    wire::LinkControlDecodeReportV1 zero_report{};
    wire::LinkControlParsedV1 zero_parsed{};
    link::LinkHelloV1 zero_value{};
    check(wire::parse_link_hello_v1(
              zero_signature.data(), zero_signature.size(), expected,
              wire::validate_p256_uncompressed_point_callback, nullptr,
              &zero_report, &zero_parsed, &zero_value) ==
              wire::Status::InvalidField &&
              zero_report.issue == wire::LinkControlIssueV1::Signature,
          "stage 1 still rejects a non-canonical zero signature");
}

} // namespace

int main()
{
    golden_initiator_hello();
    golden_responder_hello();
    exact_field_offsets();
    stream_is_never_advertised();
    peer_capability_gate();
    structural_negatives();
    identity_and_binding_negatives();
    signature_negatives();
    wrong_kind_and_constants();
    two_stage_parse_then_accept();

    if (failures != 0) {
        std::fprintf(stderr, "%d link hello codec checks failed\n", failures);
        return 1;
    }
    std::puts("link hello codec tests passed");
    return 0;
}
