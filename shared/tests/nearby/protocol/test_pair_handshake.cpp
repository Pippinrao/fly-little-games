#include "pair_handshake.hpp"
#include "pair_reveal.hpp"
#include "pair_secure.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>

using flynes::session::wire::PairCommitV1;
using flynes::session::wire::PairContextV1;
using flynes::session::wire::PairContributionV1;
using flynes::session::wire::PairRoleV1;
using flynes::session::wire::PairExchangeResultV1;
using flynes::session::wire::PairExchangeV1;
using flynes::session::wire::Status;

namespace {

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void be16(std::uint8_t* out, std::uint16_t value)
{
    out[0] = static_cast<std::uint8_t>(value >> 8u);
    out[1] = static_cast<std::uint8_t>(value);
}

void be32(std::uint8_t* out, std::uint32_t value)
{
    out[0] = static_cast<std::uint8_t>(value >> 24u);
    out[1] = static_cast<std::uint8_t>(value >> 16u);
    out[2] = static_cast<std::uint8_t>(value >> 8u);
    out[3] = static_cast<std::uint8_t>(value);
}

std::array<std::uint8_t, 65> generator()
{
    const char* value =
        "046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
        "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5";
    std::array<std::uint8_t, 65> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::uint8_t>(std::stoul(std::string(value + i * 2, 2), nullptr, 16));
    return result;
}

bool validate_known_point(void* context, const std::uint8_t point[65])
{
    ++*static_cast<int*>(context);
    const auto expected = generator();
    return std::equal(expected.begin(), expected.end(), point);
}

std::array<std::uint8_t, 80> make_context()
{
    std::array<std::uint8_t, 80> bytes{};
    be16(bytes.data(), 1);
    be16(bytes.data() + 8, 2);
    be16(bytes.data() + 10, 0);
    bytes[12] = 1;
    std::fill(bytes.begin() + 16, bytes.begin() + 32, std::uint8_t{0x11});
    std::fill(bytes.begin() + 32, bytes.begin() + 48, std::uint8_t{0x22});
    be32(bytes.data() + 48, 60000);
    std::fill(bytes.begin() + 64, bytes.end(), std::uint8_t{0x33});
    return bytes;
}

std::array<std::uint8_t, 320> make_contribution(
    const PairContextV1& context, PairRoleV1 role)
{
    std::array<std::uint8_t, 320> bytes{};
    be16(bytes.data(), 1);
    be16(bytes.data() + 8, 2);
    be16(bytes.data() + 10, 0);
    bytes[12] = static_cast<std::uint8_t>(role);
    std::copy(context.hash.begin(), context.hash.end(), bytes.begin() + 16);
    std::copy(context.bytes.begin() + 16, context.bytes.begin() + 48, bytes.begin() + 48);
    be32(bytes.data() + 80, 60000);
    const auto point = generator();
    std::copy(point.begin(), point.end(), bytes.begin() + 88);
    std::copy(point.begin(), point.end(), bytes.begin() + 153);
    std::fill(bytes.begin() + 218, bytes.begin() + 250, std::uint8_t{0x44});
    std::fill(bytes.begin() + 250, bytes.begin() + 282, std::uint8_t{0x55});
    std::fill(bytes.begin() + 282, bytes.begin() + 314, std::uint8_t{0x66});
    return bytes;
}

std::array<std::uint8_t, 152> make_commit(const PairContextV1& context,
                                           PairRoleV1 sender,
                                           const std::array<std::uint8_t, 320>& contribution)
{
    std::array<std::uint8_t, 152> bytes{};
    be16(bytes.data(), 1);
    std::copy(context.hash.begin(), context.hash.end(), bytes.begin() + 8);
    bytes[40] = static_cast<std::uint8_t>(sender);
    bytes[41] = static_cast<std::uint8_t>(sender == PairRoleV1::Initiator
                                              ? PairRoleV1::Responder
                                              : PairRoleV1::Initiator);
    const auto commitment = flynes::session::wire::pair_commitment_v1(
        contribution.data(), contribution.size());
    std::copy(commitment.begin(), commitment.end(), bytes.begin() + 48);
    std::copy(contribution.begin() + 153, contribution.begin() + 218,
              bytes.begin() + 80);
    return bytes;
}

void test_context_contribution_and_commit()
{
    const auto context_bytes = make_context();
    PairContextV1 context{};
    check(flynes::session::wire::decode_pair_context_v1(
              context_bytes.data(), context_bytes.size(), &context) == Status::Ok,
          "canonical PairContext accepted");

    auto contribution_bytes = make_contribution(context, PairRoleV1::Initiator);
    std::array<std::uint8_t, 32> tls_hash{};
    std::array<std::uint8_t, 32> nonce{};
    std::array<std::uint8_t, 32> capability_hash{};
    std::fill(tls_hash.begin(), tls_hash.end(), std::uint8_t{0x44});
    std::fill(nonce.begin(), nonce.end(), std::uint8_t{0x55});
    std::fill(capability_hash.begin(), capability_hash.end(), std::uint8_t{0x66});
    const auto point = generator();
    std::array<std::uint8_t, 320> encoded_contribution{};
    int encoder_point_checks = 0;
    check(flynes::session::wire::encode_pair_contribution_v1(
              context, PairRoleV1::Initiator, point, point, tls_hash, nonce,
              capability_hash, validate_known_point, &encoder_point_checks,
              &encoded_contribution) == Status::Ok &&
              encoder_point_checks == 2 &&
              encoded_contribution == contribution_bytes,
          "canonical contribution encoder matches the frozen 320-byte wire");
    PairContributionV1 contribution{};
    int point_checks = 0;
    check(flynes::session::wire::decode_pair_contribution_v1(
              contribution_bytes.data(), contribution_bytes.size(), context,
              PairRoleV1::Initiator, validate_known_point, &point_checks,
              &contribution) == Status::Ok && point_checks == 2,
          "contribution requires provider-independent point validation twice");

    auto commit_bytes = make_commit(context, PairRoleV1::Initiator, contribution_bytes);
    std::array<std::uint8_t, 152> encoded_commit{};
    check(flynes::session::wire::encode_pair_commit_v1(
              context, contribution, validate_known_point,
              &encoder_point_checks, &encoded_commit) == Status::Ok &&
              encoded_commit == commit_bytes,
          "canonical commit encoder matches the frozen 152-byte wire");
    PairCommitV1 commit{};
    check(flynes::session::wire::decode_pair_commit_v1(
              commit_bytes.data(), commit_bytes.size(), context,
              PairRoleV1::Initiator, validate_known_point, &point_checks,
              &commit) == Status::Ok,
          "canonical PAIR_COMMIT accepted");
    check(flynes::session::wire::bind_reveal_to_commit_v1(commit, contribution) ==
              Status::Ok,
          "reveal binds commitment and exact advertised ECDH point");

    contribution_bytes[250] ^= 1;
    check(flynes::session::wire::decode_pair_contribution_v1(
              contribution_bytes.data(), contribution_bytes.size(), context,
              PairRoleV1::Initiator, validate_known_point, &point_checks,
              &contribution) == Status::Ok &&
              flynes::session::wire::bind_reveal_to_commit_v1(commit, contribution) !=
                  Status::Ok,
          "post-commit reveal mutation is rejected");
}

void test_negative_fields_and_transcript()
{
    auto context_bytes = make_context();
    PairContextV1 context{};
    flynes::session::wire::decode_pair_context_v1(
        context_bytes.data(), context_bytes.size(), &context);
    auto initiator_bytes = make_contribution(context, PairRoleV1::Initiator);
    auto responder_bytes = make_contribution(context, PairRoleV1::Responder);
    PairContributionV1 initiator{};
    PairContributionV1 responder{};
    int point_checks = 0;
    flynes::session::wire::decode_pair_contribution_v1(
        initiator_bytes.data(), initiator_bytes.size(), context,
        PairRoleV1::Initiator, validate_known_point, &point_checks, &initiator);
    flynes::session::wire::decode_pair_contribution_v1(
        responder_bytes.data(), responder_bytes.size(), context,
        PairRoleV1::Responder, validate_known_point, &point_checks, &responder);

    auto bad = initiator_bytes;
    bad[13] = 1;
    PairContributionV1 rejected{};
    check(flynes::session::wire::decode_pair_contribution_v1(
              bad.data(), bad.size(), context, PairRoleV1::Initiator,
              validate_known_point, &point_checks, &rejected) == Status::NonzeroReserved,
          "nonzero contribution reserved bytes rejected");
    bad = initiator_bytes;
    bad[12] = static_cast<std::uint8_t>(PairRoleV1::Responder);
    check(flynes::session::wire::decode_pair_contribution_v1(
              bad.data(), bad.size(), context, PairRoleV1::Initiator,
              validate_known_point, &point_checks, &rejected) == Status::InvalidField,
          "reflected role rejected");

    const auto initiator_commit = flynes::session::wire::pair_commitment_v1(
        initiator.bytes.data(), initiator.bytes.size());
    const auto responder_commit = flynes::session::wire::pair_commitment_v1(
        responder.bytes.data(), responder.bytes.size());
    std::array<std::uint8_t, 752> preimage{};
    std::array<std::uint8_t, 32> transcript_hash{};
    check(flynes::session::wire::build_pair_transcript_preimage_v1(
              1, {}, initiator_commit, responder_commit, initiator, responder,
              &preimage, &transcript_hash) == Status::Ok &&
              preimage[8] == 1 &&
              std::equal(initiator.bytes.begin(), initiator.bytes.end(),
                         preimage.begin() + 112) &&
              std::equal(responder.bytes.begin(), responder.bytes.end(),
                         preimage.begin() + 432),
          "BLE transcript uses exact 752-byte canonical role ordering");
    std::array<std::uint8_t, 32> nonzero{};
    nonzero[0] = 1;
    check(flynes::session::wire::build_pair_transcript_preimage_v1(
              1, nonzero, initiator_commit, responder_commit, initiator, responder,
              &preimage, &transcript_hash) == Status::InvalidField,
          "BLE transcript rejects QR entry context");

    std::array<std::uint8_t, 64> initiator_signature{};
    std::array<std::uint8_t, 64> responder_signature{};
    initiator_signature[31] = 1;
    initiator_signature[63] = 1;
    responder_signature[31] = 2;
    responder_signature[63] = 2;
    std::array<std::uint8_t, 880> transcript{};
    std::array<std::uint8_t, 32> object_hash{};
    check(flynes::session::wire::build_pair_transcript_object_v1(
              preimage, transcript_hash, initiator_signature,
              responder_signature, &transcript, &object_hash) == Status::Ok &&
              std::equal(preimage.begin(), preimage.end(), transcript.begin()) &&
              std::equal(initiator_signature.begin(), initiator_signature.end(),
                         transcript.begin() + 752) &&
              std::equal(responder_signature.begin(), responder_signature.end(),
                         transcript.begin() + 816) &&
              object_hash == flynes::session::wire::domain_hash(
                  "flynes-pair-transcript-object-v1", transcript.data(),
                  transcript.size()),
          "registered PairTranscriptV1 is exact preimage plus role-ordered signatures");
    auto wrong_hash = transcript_hash;
    wrong_hash[0] ^= 0x80;
    check(flynes::session::wire::build_pair_transcript_object_v1(
              preimage, wrong_hash, initiator_signature, responder_signature,
              &transcript, &object_hash) == Status::InvalidField,
          "PairTranscriptV1 rejects a preimage hash mismatch");
    auto high_s = responder_signature;
    high_s.fill(0xff);
    check(flynes::session::wire::build_pair_transcript_object_v1(
              preimage, transcript_hash, initiator_signature, high_s,
              &transcript, &object_hash) == Status::InvalidField,
          "PairTranscriptV1 rejects a noncanonical signature");
}

void test_exchange_enforces_fixed_commit_reveal_order()
{
    const auto context_bytes = make_context();
    PairContextV1 context{};
    check(flynes::session::wire::decode_pair_context_v1(
              context_bytes.data(), context_bytes.size(), &context) == Status::Ok,
          "exchange fixture context decodes");
    const auto initiator = make_contribution(context, PairRoleV1::Initiator);
    const auto responder = make_contribution(context, PairRoleV1::Responder);
    const auto initiator_commit = make_commit(
        context, PairRoleV1::Initiator, initiator);
    const auto responder_commit = make_commit(
        context, PairRoleV1::Responder, responder);
    int point_checks = 0;
    PairExchangeV1 exchange(validate_known_point, &point_checks);
    check(exchange.begin(context_bytes.data(), context_bytes.size()) ==
              PairExchangeResultV1::Accepted,
          "pair exchange begins from exact PairContext");
    check(exchange.accept_commit(
              PairRoleV1::Initiator, initiator_commit.data(),
              initiator_commit.size()) == PairExchangeResultV1::Accepted &&
              exchange.accept_commit(
                  PairRoleV1::Responder, responder_commit.data(),
                  responder_commit.size()) == PairExchangeResultV1::Accepted,
          "commits are accepted only in initiator then responder order");
    std::array<std::uint8_t, 32> initiator_hash{};
    std::array<std::uint8_t, 32> responder_hash{};
    initiator_hash[0] = 1;
    responder_hash[0] = 2;
    check(exchange.accept_reveal(
              PairRoleV1::Initiator, initiator.data(), initiator.size(),
              initiator_hash) == PairExchangeResultV1::Accepted &&
              exchange.accept_reveal(
                  PairRoleV1::Responder, responder.data(), responder.size(),
                  responder_hash) == PairExchangeResultV1::Ready &&
              exchange.ready() &&
              exchange.initiator_commit().commitment ==
                  exchange.initiator_contribution().commitment &&
              exchange.responder_commit().commitment ==
                  exchange.responder_contribution().commitment &&
              exchange.initiator_reveal_hash() == initiator_hash &&
              exchange.responder_reveal_hash() == responder_hash,
          "reveals bind their commits and seal one ordered prelude");

    PairExchangeV1 reflected(validate_known_point, &point_checks);
    check(reflected.begin(context_bytes.data(), context_bytes.size()) ==
              PairExchangeResultV1::Accepted &&
              reflected.accept_commit(
                  PairRoleV1::Responder, responder_commit.data(),
                  responder_commit.size()) ==
                  PairExchangeResultV1::ProtocolViolation &&
              reflected.failed(),
          "responder-first reflection fails the prelude closed");
}

void test_pair_reveal_wire_contract()
{
    const auto context_bytes = make_context();
    PairContextV1 context{};
    check(flynes::session::wire::decode_pair_context_v1(
              context_bytes.data(), context_bytes.size(), &context) == Status::Ok,
          "reveal fixture context decodes");
    const auto initiator_bytes = make_contribution(
        context, PairRoleV1::Initiator);
    const auto responder_bytes = make_contribution(
        context, PairRoleV1::Responder);
    const auto initiator_commit_bytes = make_commit(
        context, PairRoleV1::Initiator, initiator_bytes);
    const auto responder_commit_bytes = make_commit(
        context, PairRoleV1::Responder, responder_bytes);
    PairCommitV1 initiator_commit{};
    PairCommitV1 responder_commit{};
    int point_checks = 0;
    check(flynes::session::wire::decode_pair_commit_v1(
              initiator_commit_bytes.data(), initiator_commit_bytes.size(),
              context, PairRoleV1::Initiator, validate_known_point,
              &point_checks, &initiator_commit) == Status::Ok &&
              flynes::session::wire::decode_pair_commit_v1(
                  responder_commit_bytes.data(), responder_commit_bytes.size(),
                  context, PairRoleV1::Responder, validate_known_point,
                  &point_checks, &responder_commit) == Status::Ok,
          "reveal fixture commits decode");

    std::array<std::uint8_t,
               flynes::session::wire::kPairRevealAadSizeV1> aad{};
    const std::array<std::uint8_t, 16> service{{
        0xe7,0x38,0xdc,0xda,0xa2,0x1a,0x58,0x2b,
        0x9c,0xed,0xa9,0x7f,0x6f,0xeb,0xee,0xdd}};
    check(flynes::session::wire::build_pair_reveal_aad_v1(
              context, initiator_commit, responder_commit,
              PairRoleV1::Initiator, &aad) == Status::Ok &&
              std::equal(service.begin(), service.end(), aad.begin()) &&
              aad[16] == 0 && aad[17] == 2 &&
              aad[18] == 0 && aad[19] == 0 && aad[20] == 5 &&
              aad[21] == 1 && aad[22] == 2 && aad[23] == 0 &&
              std::equal(context.hash.begin(), context.hash.end(),
                         aad.begin() + 24) &&
              std::equal(initiator_commit.commitment.begin(),
                         initiator_commit.commitment.end(), aad.begin() + 56) &&
              std::equal(responder_commit.commitment.begin(),
                         responder_commit.commitment.end(), aad.begin() + 88) &&
              aad[120] == 0 && aad[121] == 0 &&
              aad[122] == 1 && aad[123] == 0x40,
          "PAIR_REVEAL AAD is the exact frozen 124-byte sequence");

    std::array<std::uint8_t, 12> nonce{};
    std::array<std::uint8_t,
               flynes::session::wire::kPairRevealCiphertextAndTagSizeV1>
        ciphertext{};
    for (std::size_t index = 0; index < nonce.size(); ++index)
        nonce[index] = static_cast<std::uint8_t>(index + 1);
    for (std::size_t index = 0; index < ciphertext.size(); ++index)
        ciphertext[index] = static_cast<std::uint8_t>(index);
    std::array<std::uint8_t,
               flynes::session::wire::kPairRevealBodySizeV1> envelope{};
    check(flynes::session::wire::encode_pair_reveal_envelope_v1(
              nonce, ciphertext, &envelope) == Status::Ok &&
              envelope[0] == 1 && envelope[1] == 0 && envelope[2] == 0 &&
              envelope[3] == 0 &&
              std::equal(nonce.begin(), nonce.end(), envelope.begin() + 4) &&
              std::equal(ciphertext.begin(), ciphertext.end(),
                         envelope.begin() + 16),
          "PAIR_REVEAL envelope has the exact 352-byte layout");
    flynes::session::wire::PairRevealEnvelopeV1 decoded{};
    check(flynes::session::wire::decode_pair_reveal_envelope_v1(
              envelope.data(), envelope.size(), &decoded) == Status::Ok &&
              decoded.public_nonce == nonce &&
              decoded.ciphertext_and_tag == ciphertext,
          "PAIR_REVEAL envelope round trips exactly");
    envelope[2] = 1;
    check(flynes::session::wire::decode_pair_reveal_envelope_v1(
              envelope.data(), envelope.size(), &decoded) ==
              Status::NonzeroReserved &&
              flynes::session::wire::decode_pair_reveal_envelope_v1(
                  envelope.data(), envelope.size() - 1, &decoded) ==
              Status::Truncated,
          "PAIR_REVEAL rejects reserved and size mutations");
}

void test_pair_signature_and_secure_envelope_contract()
{
    const auto point = generator();
    std::array<std::uint8_t, 32> transcript{};
    transcript[0] = 0x41;
    std::array<std::uint8_t, 64> signature{};
    signature[31] = 1;
    signature[63] = 1;
    std::array<std::uint8_t,
               flynes::session::wire::kPairSignatureInnerSizeV1> inner{};
    int validations = 0;
    check(flynes::session::wire::encode_pair_signature_inner_v1(
              transcript, PairRoleV1::Initiator, point, point, signature,
              validate_known_point, &validations, &inner) == Status::Ok &&
              inner[0] == 0 && inner[1] == 1 && inner[40] == 1 &&
              inner[41] == 2 && validations == 2,
          "PAIR_SIGNATURE inner encodes canonical roles and provider keys");
    flynes::session::wire::PairSignatureInnerV1 decoded{};
    check(flynes::session::wire::decode_pair_signature_inner_v1(
              inner.data(), inner.size(), transcript,
              PairRoleV1::Initiator, point, point, validate_known_point,
              &validations, &decoded) == Status::Ok &&
              decoded.signature == signature && decoded.sender_key_id !=
                  std::array<std::uint8_t, 32>{},
          "PAIR_SIGNATURE inner round trips with computed key IDs");
    auto bad_inner = inner;
    bad_inner[42] = 1;
    check(flynes::session::wire::decode_pair_signature_inner_v1(
              bad_inner.data(), bad_inner.size(), transcript,
              PairRoleV1::Initiator, point, point, validate_known_point,
              &validations, &decoded) == Status::NonzeroReserved &&
              flynes::session::wire::decode_pair_signature_inner_v1(
                  inner.data(), inner.size() - 1, transcript,
                  PairRoleV1::Initiator, point, point, validate_known_point,
                  &validations, &decoded) == Status::Truncated,
          "PAIR_SIGNATURE rejects reserved and size mutations");

    std::array<std::uint8_t,
               flynes::session::wire::kPairSecureNonceSizeV1> nonce{};
    std::array<std::uint8_t,
               flynes::session::wire::kPairSecureAadSizeV1> aad{};
    check(flynes::session::wire::build_pair_secure_nonce_v1(
              PairRoleV1::Initiator, 1, &nonce) == Status::Ok &&
              nonce[0] == 0x50 && nonce[1] == 0x32 && nonce[2] == 0x43 &&
              nonce[3] == 0x01 && nonce[11] == 1 &&
              flynes::session::wire::build_pair_secure_aad_v1(
                  6, PairRoleV1::Initiator, transcript, 1, &aad) ==
                  Status::Ok &&
              aad[20] == 6 && aad[21] == 1 && aad[22] == 2 &&
              aad[63] == 1 && aad[66] == 0 && aad[67] == 176,
          "PairSecure freezes direction nonce and exact 68-byte AAD");

    std::array<std::uint8_t,
               flynes::session::wire::kPairSignatureInnerSizeV1 + 16>
        ciphertext{};
    ciphertext[0] = 0x51;
    std::vector<std::uint8_t> envelope;
    check(flynes::session::wire::encode_pair_secure_envelope_v1(
              6, 1, ciphertext.data(), ciphertext.size(), &envelope) ==
              Status::Ok && envelope.size() == 204 && envelope[0] == 1 &&
              envelope[11] == 1 && envelope[12] == 0x51,
          "PairSecure type 6 envelope has the exact 204-byte body");
    flynes::session::wire::PairSecureEnvelopeV1 decoded_envelope{};
    check(flynes::session::wire::decode_pair_secure_envelope_v1(
              6, envelope.data(), envelope.size(), &decoded_envelope) ==
              Status::Ok && decoded_envelope.message_counter == 1 &&
              decoded_envelope.ciphertext_and_tag ==
                  std::vector<std::uint8_t>(ciphertext.begin(),
                                            ciphertext.end()),
          "PairSecure type 6 envelope round trips exactly");
    envelope[1] = 1;
    check(flynes::session::wire::decode_pair_secure_envelope_v1(
              6, envelope.data(), envelope.size(), &decoded_envelope) ==
              Status::NonzeroReserved &&
              flynes::session::wire::decode_pair_secure_envelope_v1(
                  6, envelope.data(), envelope.size() - 1,
                  &decoded_envelope) == Status::Truncated &&
              flynes::session::wire::decode_pair_secure_envelope_v1(
                  8, envelope.data(), envelope.size(), &decoded_envelope) ==
              Status::UnknownKind,
          "PairSecure rejects reserved, truncation, and non-secure types");
}

void test_key_confirm_inner_contract()
{
    const auto point = generator();
    std::array<std::uint8_t, 32> transcript{};
    transcript[0] = 0x61;
    std::array<std::uint8_t, 32> tag{};
    tag[0] = 0x62;
    std::array<std::uint8_t,
               flynes::session::wire::kKeyConfirmInnerSizeV1> inner{};
    int validations = 0;
    check(flynes::session::wire::encode_key_confirm_inner_v1(
              1, 1, transcript,
              PairRoleV1::Initiator, point, point, tag,
              validate_known_point, &validations, &inner) == Status::Ok &&
              inner[0] == 0 && inner[1] == 1 && inner[8] == 1 &&
              inner[9] == 1 &&
              inner[10] == 1 && inner[11] == 2 && inner[112] == 0x62,
          "KEY_CONFIRM freezes the exact 112-byte body and 32-byte tag");
    std::vector<std::uint8_t> hmac_input;
    check(flynes::session::wire::build_key_confirm_hmac_input_v1(
              inner.data(), flynes::session::wire::kKeyConfirmBodySizeV1,
              &hmac_input) == Status::Ok && hmac_input.size() == 137 &&
              std::string(hmac_input.begin(), hmac_input.begin() + 21) ==
                  "flynes-key-confirm-v1" && hmac_input[24] == 112 &&
              std::equal(inner.begin(), inner.begin() + 112,
                         hmac_input.begin() + 25),
          "KEY_CONFIRM HMAC input is domain, u32be length, and exact body");
    flynes::session::wire::KeyConfirmInnerV1 decoded{};
    check(flynes::session::wire::decode_key_confirm_inner_v1(
              inner.data(), inner.size(), 1,
              1, transcript,
              PairRoleV1::Initiator, point, point, validate_known_point,
              &validations, &decoded) == Status::Ok && decoded.tag == tag,
          "KEY_CONFIRM inner round trips with computed key IDs");
    auto mutated = inner;
    mutated[12] = 1;
    check(flynes::session::wire::decode_key_confirm_inner_v1(
              mutated.data(), mutated.size(), 1,
              1, transcript,
              PairRoleV1::Initiator, point, point, validate_known_point,
              &validations, &decoded) == Status::NonzeroReserved,
          "KEY_CONFIRM rejects reserved-byte mutation");
}

void test_pair_capability_inner_contract()
{
    std::array<std::uint8_t, 32> transcript{};
    transcript[0] = 0x71;
    std::array<std::uint8_t, 512> summary{};
    summary[1] = 1;
    summary[8] = 1;
    summary[32] = 1;
    summary[64] = 2;
    std::array<std::uint8_t, 32> tag{};
    tag[0] = 0x72;
    std::array<std::uint8_t,
               flynes::session::wire::kPairCapabilityInnerSizeV1> inner{};
    check(flynes::session::wire::encode_pair_capability_inner_v1(
              transcript, PairRoleV1::Initiator, summary, tag, &inner) ==
              Status::Ok && inner[40] == 1 && inner[41] == 2 &&
              inner[48] == summary[0] && inner[560] == 0x72,
          "PAIR_CAPABILITY_REVEAL freezes pretag, summary, and HMAC tag");
    std::vector<std::uint8_t> hmac_input;
    check(flynes::session::wire::build_pair_capability_hmac_input_v1(
              inner.data(),
              flynes::session::wire::kPairCapabilityPretagSizeV1,
              &hmac_input) == Status::Ok && hmac_input.size() == 596 &&
              std::string(hmac_input.begin(), hmac_input.begin() + 32) ==
                  "flynes-pair-capability-reveal-v1" &&
              hmac_input[35] == 48 &&
              std::equal(inner.begin(), inner.begin() + 560,
                         hmac_input.begin() + 36),
          "capability HMAC input binds exact 560-byte pretag");
    flynes::session::wire::PairCapabilityInnerV1 decoded{};
    check(flynes::session::wire::decode_pair_capability_inner_v1(
              inner.data(), inner.size(), transcript,
              PairRoleV1::Initiator, &decoded) == Status::Ok &&
              decoded.summary == summary && decoded.tag == tag,
          "PAIR_CAPABILITY_REVEAL inner round trips exactly");
    inner[42] = 1;
    check(flynes::session::wire::decode_pair_capability_inner_v1(
              inner.data(), inner.size(), transcript,
              PairRoleV1::Initiator, &decoded) == Status::NonzeroReserved,
          "capability reveal rejects reserved-byte mutation");
}

void test_initial_bearer_plan_inner_contracts()
{
    using namespace flynes::session::wire;
    std::array<std::uint8_t, 32> transcript{};
    std::array<std::uint8_t, 32> initiator_capability{};
    std::array<std::uint8_t, 32> responder_capability{};
    std::array<std::uint8_t, 16> nonce{};
    std::array<std::uint8_t, 32> tag{};
    transcript[0] = 0x81;
    initiator_capability[0] = 0x82;
    responder_capability[0] = 0x83;
    nonce[0] = 0x84;
    tag[0] = 0x85;
    BearerPlanBytes plan{};
    plan[0] = 2;
    plan[1] = 1;
    plan[2] = 1;
    plan[3] = 2;
    plan[4] = 1;
    plan[6] = 10;
    plan[11] = 1;
    plan[12] = 1;

    std::array<std::uint8_t, kInitialBearerPlanInnerSizeV1> proposal{};
    check(encode_initial_bearer_plan_inner_v1(
              transcript, initiator_capability, responder_capability,
              plan, nonce, tag, &proposal) == Status::Ok &&
              proposal[104] == 2 && proposal[152] == 0x84 &&
              proposal[168] == 1 && proposal[169] == 2 &&
              proposal[176] == 0x85,
          "INITIAL_BEARER_PLAN freezes exact 176-byte pretag and tag");
    std::vector<std::uint8_t> input;
    check(build_initial_bearer_plan_hmac_input_v1(
              proposal.data(), kInitialBearerPlanPretagSizeV1, &input) ==
              Status::Ok && input.size() == 209 && input[32] == 176 &&
              std::equal(proposal.begin(), proposal.begin() + 176,
                         input.begin() + 33),
          "INITIAL_BEARER_PLAN HMAC binds exact pretag");
    InitialBearerPlanInnerV1 decoded_proposal{};
    check(decode_initial_bearer_plan_inner_v1(
              proposal.data(), proposal.size(), transcript,
              initiator_capability, responder_capability, plan,
              &decoded_proposal) == Status::Ok &&
              decoded_proposal.plan_nonce == nonce &&
              decoded_proposal.tag == tag,
          "INITIAL_BEARER_PLAN round trips exact authenticated fields");

    const auto plan_hash = selected_bearer_plan_hash_v1(plan);
    std::array<std::uint8_t, 32> proposal_hash{};
    proposal_hash[0] = 0x86;
    std::array<std::uint8_t, kInitialBearerPlanAckInnerSizeV1> ack{};
    check(encode_initial_bearer_plan_ack_inner_v1(
              transcript, proposal_hash, plan_hash, tag, &ack) == Status::Ok &&
              ack[104] == 2 && ack[105] == 1 && ack[112] == 0x85,
          "INITIAL_BEARER_PLAN_ACK freezes responder-to-initiator roles");
    check(build_initial_bearer_plan_ack_hmac_input_v1(
              ack.data(), kInitialBearerPlanAckPretagSizeV1, &input) ==
              Status::Ok && input.size() == 149 && input[36] == 112,
          "INITIAL_BEARER_PLAN_ACK HMAC binds exact 112-byte pretag");
    InitialBearerPlanAckInnerV1 decoded_ack{};
    check(decode_initial_bearer_plan_ack_inner_v1(
              ack.data(), ack.size(), transcript, proposal_hash, plan_hash,
              &decoded_ack) == Status::Ok && decoded_ack.tag == tag,
          "INITIAL_BEARER_PLAN_ACK round trips exact hashes");

    std::array<std::uint8_t, 32> ack_hash{};
    ack_hash[0] = 0x87;
    std::array<std::uint8_t, kInitialBearerPlanFinalInnerSizeV1> final{};
    check(encode_initial_bearer_plan_final_inner_v1(
              transcript, proposal_hash, plan_hash, ack_hash, tag, &final) ==
              Status::Ok && final[136] == 1 && final[137] == 2 &&
              final[144] == 0x85,
          "INITIAL_BEARER_PLAN_FINAL freezes initiator-to-responder roles");
    check(build_initial_bearer_plan_final_hmac_input_v1(
              final.data(), kInitialBearerPlanFinalPretagSizeV1, &input) ==
              Status::Ok && input.size() == 183 && input[38] == 144,
          "INITIAL_BEARER_PLAN_FINAL HMAC binds exact 144-byte pretag");
    InitialBearerPlanFinalInnerV1 decoded_final{};
    check(decode_initial_bearer_plan_final_inner_v1(
              final.data(), final.size(), transcript, proposal_hash,
              plan_hash, ack_hash, &decoded_final) == Status::Ok &&
              decoded_final.tag == tag,
          "INITIAL_BEARER_PLAN_FINAL round trips exact hashes");

    proposal[170] = 1;
    check(decode_initial_bearer_plan_inner_v1(
              proposal.data(), proposal.size(), transcript,
              initiator_capability, responder_capability, plan,
              &decoded_proposal) == Status::NonzeroReserved,
          "initial plan rejects reserved-byte mutation");
}

void test_known_status_and_branch_contracts()
{
    using namespace flynes::session::wire;
    const auto point = generator();
    std::array<std::uint8_t, 32> transcript{};
    transcript[0] = 0x41;
    std::array<std::uint8_t, 32> tag{};
    tag[0] = 0x42;
    int validations = 0;
    std::array<std::uint8_t, kKnownStatusInnerSizeV1> status{};
    check(encode_known_status_inner_v1(
              transcript, PairRoleV1::Initiator, true, point, point, tag,
              validate_known_point, &validations, &status) == Status::Ok &&
              status[40] == 1 && status[41] == 2 && status[42] == 1 &&
              status[43] == 0 && status[112] == 0x42,
          "KNOWN_STATUS freezes roles, one-bit status, ids, and tag offsets");
    KnownStatusInnerV1 decoded_status{};
    check(decode_known_status_inner_v1(
              status.data(), status.size(), transcript,
              PairRoleV1::Initiator, point, point, validate_known_point,
              &validations, &decoded_status) == Status::Ok &&
              decoded_status.known && decoded_status.tag == tag,
          "KNOWN_STATUS exact inner round trips");
    std::vector<std::uint8_t> hmac_input;
    check(build_known_status_hmac_input_v1(
              status.data(), kKnownStatusBodySizeV1, &hmac_input) == Status::Ok &&
              hmac_input.size() > kKnownStatusBodySizeV1 &&
              std::equal(status.begin(), status.begin() + 112,
                         hmac_input.end() - 112),
          "KNOWN_STATUS HMAC binds its exact 112-byte body");
    auto bad_status = status;
    bad_status[42] = 2;
    check(decode_known_status_inner_v1(
              bad_status.data(), bad_status.size(), transcript,
              PairRoleV1::Initiator, point, point, validate_known_point,
              &validations, &decoded_status) == Status::InvalidField,
          "KNOWN_STATUS rejects non-boolean status");

    std::array<std::uint8_t, 64> zero_proof{};
    std::array<std::uint8_t, kKnownBranchInnerSizeV1> branch{};
    check(encode_known_branch_inner_v1(
              transcript, PairRoleV1::Responder,
              KnownBranchKindV1::SasFallback, point, point, zero_proof,
              validate_known_point, &validations, &branch) == Status::Ok &&
              branch[40] == 2 && branch[41] == 1 && branch[42] == 2 &&
              std::all_of(branch.begin() + 112, branch.end(),
                          [](std::uint8_t value) { return value == 0; }),
          "KNOWN_BRANCH fallback has the fixed shape and zero proof");
    KnownBranchInnerV1 decoded_branch{};
    check(decode_known_branch_inner_v1(
              branch.data(), branch.size(), transcript,
              PairRoleV1::Responder, KnownBranchKindV1::SasFallback,
              point, point, validate_known_point, &validations,
              &decoded_branch) == Status::Ok,
          "KNOWN_BRANCH fallback round trips exactly");
    branch[112] = 1;
    check(decode_known_branch_inner_v1(
              branch.data(), branch.size(), transcript,
              PairRoleV1::Responder, KnownBranchKindV1::SasFallback,
              point, point, validate_known_point, &validations,
              &decoded_branch) == Status::InvalidField,
          "KNOWN_BRANCH fallback rejects a nonzero pseudo-signature");

    std::array<std::uint8_t, 64> signature{};
    signature[31] = 1;
    signature[63] = 1;
    check(encode_known_branch_inner_v1(
              transcript, PairRoleV1::Initiator,
              KnownBranchKindV1::Verified, point, point, signature,
              validate_known_point, &validations, &branch) == Status::Ok,
          "KNOWN_BRANCH verified accepts a canonical low-S proof");
    std::array<std::uint8_t, 32> digest{};
    check(known_branch_digest_v1(
              branch.data(), kKnownBranchBodySizeV1, &digest) == Status::Ok &&
              std::any_of(digest.begin(), digest.end(),
                          [](std::uint8_t value) { return value != 0; }),
          "KNOWN_BRANCH proof digest covers the exact 112-byte body");
}

} // namespace

int main()
{
    test_context_contribution_and_commit();
    test_negative_fields_and_transcript();
    test_exchange_enforces_fixed_commit_reveal_order();
    test_pair_reveal_wire_contract();
    test_pair_signature_and_secure_envelope_contract();
    test_key_confirm_inner_contract();
    test_pair_capability_inner_contract();
    test_initial_bearer_plan_inner_contracts();
    test_known_status_and_branch_contracts();
    if (failures != 0)
        return 1;
    std::cout << "pair context/commit/reveal/transcript contract passed\n";
    return 0;
}
