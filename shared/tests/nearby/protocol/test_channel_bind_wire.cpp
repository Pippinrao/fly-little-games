#include "channel_bind.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace {
using namespace flynes::session::wire;

int failures = 0;

void check(bool value, const char* message)
{
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::array<std::uint8_t, 65> public_key(std::uint8_t scalar)
{
    // Valid P-256 points for scalar 1 and 2.
    static constexpr std::array<std::uint8_t, 65> generator = {{
        0x04, 0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42,
        0x47, 0xf8, 0xbc, 0xe6, 0xe5, 0x63, 0xa4, 0x40,
        0xf2, 0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33,
        0xa0, 0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2,
        0x96, 0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f,
        0x9b, 0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e,
        0x16, 0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e,
        0xce, 0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51,
        0xf5
    }};
    static constexpr std::array<std::uint8_t, 65> twice_generator = {{
        0x04, 0x7c, 0xf2, 0x7b, 0x18, 0x8d, 0x03, 0x4f,
        0x7e, 0x8a, 0x52, 0x38, 0x03, 0x04, 0xb5, 0x1a,
        0xc3, 0xc0, 0x89, 0x69, 0xe2, 0x77, 0xf2, 0x1b,
        0x35, 0xa6, 0x0b, 0x48, 0xfc, 0x47, 0x66, 0x99,
        0x78, 0x07, 0x77, 0x55, 0x10, 0xdb, 0x8e, 0xd0,
        0x40, 0x29, 0x3d, 0x9a, 0xc6, 0x9f, 0x74, 0x30,
        0xdb, 0xba, 0x7d, 0xad, 0xe6, 0x3c, 0xe9, 0x82,
        0x29, 0x9e, 0x04, 0xb7, 0x9d, 0x22, 0x78, 0x73,
        0xd1
    }};
    return scalar == 1 ? generator : twice_generator;
}

void exact_initial_bind_contract()
{
    std::array<std::uint8_t, 32> transcript{};
    std::array<std::uint8_t, 16> session{};
    std::array<std::uint8_t, 16> channel{};
    transcript[0] = 1;
    session[0] = 2;
    channel[0] = 3;

    std::array<std::uint8_t, kChannelBindSizeV1> bind{};
    check(encode_initial_channel_bind_v1(
              transcript, session, channel, &bind) == Status::Ok,
          "initial ChannelBind encodes");
    check(bind[0] == 0 && bind[1] == 1 && bind[8] == 1 &&
              bind[16] == 1 && bind[48] == 2 && bind[104] == 3,
          "initial ChannelBind uses exact field offsets");
    check(std::all_of(bind.begin() + 64, bind.begin() + 104,
                      [](std::uint8_t value) { return value == 0; }),
          "initial ChannelBind has zero reconnect fields");
    check(decode_initial_channel_bind_v1(
              bind.data(), bind.size(), transcript, session, channel) ==
              Status::Ok,
          "initial ChannelBind round trips");

    auto mutated = bind;
    mutated[9] = 1;
    check(decode_initial_channel_bind_v1(
              mutated.data(), mutated.size(), transcript, session, channel) ==
              Status::InvalidField,
          "initial ChannelBind rejects reserved bytes");
    check(decode_initial_channel_bind_v1(
              bind.data(), bind.size() - 1, transcript, session, channel) ==
              Status::Truncated,
          "initial ChannelBind rejects truncation");
    check(decode_initial_channel_bind_v1(
              bind.data(), bind.size() + 1, transcript, session, channel) ==
              Status::Trailing,
          "initial ChannelBind rejects trailing bytes");
}

void exact_proof_and_ack_contract()
{
    std::array<std::uint8_t, 32> transcript{};
    std::array<std::uint8_t, 16> session{};
    std::array<std::uint8_t, 16> channel{};
    std::array<std::uint8_t, 32> exporter{};
    std::array<std::uint8_t, 32> connector_tag{};
    std::array<std::uint8_t, 32> listener_tag{};
    transcript[0] = 1;
    session[0] = 2;
    channel[0] = 3;
    exporter[0] = 4;
    connector_tag[0] = 5;
    listener_tag[0] = 6;
    const auto initiator_key = public_key(1);
    const auto responder_key = public_key(2);

    std::array<std::uint8_t, kChannelBindSizeV1> bind{};
    check(encode_initial_channel_bind_v1(
              transcript, session, channel, &bind) == Status::Ok,
          "proof fixture bind encodes");

    std::array<std::uint8_t, kChannelBindProofBodySizeV1> connector_body{};
    check(encode_channel_bind_proof_body_v1(
              bind, PairRoleV1::Responder, responder_key, initiator_key,
              &connector_body) == Status::Ok,
          "connector proof body encodes independently of TLS role");
    check(connector_body[144] == 2 && connector_body[145] == 1,
          "proof body carries ordered Pair roles");

    std::vector<std::uint8_t> proof_input;
    check(build_channel_bind_proof_hmac_input_v1(
              connector_body.data(), connector_body.size(), exporter,
              &proof_input) == Status::Ok &&
              proof_input.size() == 28 + 4 + 216 + 32 &&
              proof_input[28] == 0 && proof_input[31] == 216,
          "proof HMAC input has exact domain, length, body and exporter");

    std::array<std::uint8_t, kChannelBindProofSizeV1> connector_proof{};
    check(encode_channel_bind_proof_v1(
              connector_body, connector_tag, &connector_proof) == Status::Ok,
          "connector proof encodes");
    std::array<std::uint8_t, 32> decoded_tag{};
    check(decode_channel_bind_proof_v1(
              connector_proof.data(), connector_proof.size(), bind,
              PairRoleV1::Responder, responder_key, initiator_key,
              &decoded_tag) == Status::Ok && decoded_tag == connector_tag,
          "connector proof round trips exact ordered identities");
    check(decode_channel_bind_proof_v1(
              connector_proof.data(), connector_proof.size(), bind,
              PairRoleV1::Initiator, initiator_key, responder_key,
              &decoded_tag) == Status::InvalidField,
          "proof rejects reflected Pair role and identity order");

    std::array<std::uint8_t, kChannelBindProofBodySizeV1> listener_body{};
    check(encode_channel_bind_proof_body_v1(
              bind, PairRoleV1::Initiator, initiator_key, responder_key,
              &listener_body) == Status::Ok,
          "listener proof body encodes");
    std::array<std::uint8_t, kChannelBindProofSizeV1> listener_proof{};
    check(encode_channel_bind_proof_v1(
              listener_body, listener_tag, &listener_proof) == Status::Ok,
          "listener proof encodes");
    const auto connector_hash = channel_bind_proof_hash_v1(connector_proof);
    const auto listener_hash = channel_bind_proof_hash_v1(listener_proof);

    std::array<std::uint8_t, kChannelBindAckBodySizeV1> ack_body{};
    check(encode_channel_bind_ack_body_v1(
              channel, connector_hash, listener_hash,
              PairRoleV1::Responder, &ack_body) == Status::Ok &&
              ack_body[88] == 2 && ack_body[89] == 1,
          "ACK body is connector-to-listener regardless of Pair role");
    std::vector<std::uint8_t> ack_input;
    check(build_channel_bind_ack_hmac_input_v1(
              ack_body.data(), ack_body.size(), exporter, &ack_input) ==
              Status::Ok && ack_input.size() == 26 + 4 + 96 + 32,
          "ACK HMAC input has exact domain, length, body and exporter");
    std::array<std::uint8_t, kChannelBindAckSizeV1> ack{};
    check(encode_channel_bind_ack_v1(
              ack_body, connector_tag, &ack) == Status::Ok,
          "ACK encodes");
    check(decode_channel_bind_ack_v1(
              ack.data(), ack.size(), channel, connector_hash, listener_hash,
              PairRoleV1::Responder, &decoded_tag) == Status::Ok &&
              decoded_tag == connector_tag,
          "ACK round trips exact proof hashes and connector role");

    const auto preamble = bind_stream_preamble_v1();
    check(preamble == std::array<std::uint8_t, 8>{{
                          0x46, 0x4e, 0x42, 0x31, 0x00, 0x01, 0x00, 0x01}},
          "bind stream preamble is exact FNB1/version/kind");
}

void invalid_inputs_fail_closed()
{
    std::array<std::uint8_t, 32> transcript{};
    std::array<std::uint8_t, 16> session{};
    std::array<std::uint8_t, 16> channel{};
    transcript[0] = session[0] = channel[0] = 1;
    std::array<std::uint8_t, kChannelBindSizeV1> bind{};
    check(encode_initial_channel_bind_v1(
              transcript, session, channel, &bind) == Status::Ok,
          "negative fixture bind encodes");
    auto invalid_key = public_key(1);
    invalid_key[0] = 3;
    std::array<std::uint8_t, kChannelBindProofBodySizeV1> body{};
    check(encode_channel_bind_proof_body_v1(
              bind, PairRoleV1::Initiator, invalid_key, public_key(2),
              &body) == Status::InvalidField,
          "proof rejects invalid P-256 identity point");
    std::array<std::uint8_t, 32> zero_tag{};
    std::array<std::uint8_t, kChannelBindProofSizeV1> proof{};
    check(encode_channel_bind_proof_v1(
              body, zero_tag, &proof) == Status::InvalidField,
          "proof rejects a zero HMAC tag");
}

} // namespace

int main()
{
    exact_initial_bind_contract();
    exact_proof_and_ack_contract();
    invalid_inputs_fail_closed();
    if (failures != 0) return 1;
    std::puts("channel bind wire tests passed");
    return 0;
}
