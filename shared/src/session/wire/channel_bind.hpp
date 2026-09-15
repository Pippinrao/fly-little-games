#ifndef FLYNES_SESSION_WIRE_CHANNEL_BIND_HPP
#define FLYNES_SESSION_WIRE_CHANNEL_BIND_HPP

#include "pair_secure.hpp"
#include "quic_contract.hpp"

#include <array>
#include <vector>

namespace flynes::session::wire {

constexpr std::size_t kChannelBindSizeV1 = 136;
constexpr std::size_t kChannelBindProofBodySizeV1 = 216;
constexpr std::size_t kChannelBindProofSizeV1 = 248;
constexpr std::size_t kChannelBindAckBodySizeV1 = 96;
constexpr std::size_t kChannelBindAckSizeV1 = 128;

Status encode_initial_channel_bind_v1(
    const std::array<std::uint8_t, 32>& transcript,
    const std::array<std::uint8_t, 16>& session,
    const std::array<std::uint8_t, 16>& channel_id,
    std::array<std::uint8_t, kChannelBindSizeV1>* out) noexcept;
Status decode_initial_channel_bind_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& transcript,
    const std::array<std::uint8_t, 16>& session,
    const std::array<std::uint8_t, 16>& channel_id) noexcept;

Status encode_channel_bind_proof_body_v1(
    const std::array<std::uint8_t, kChannelBindSizeV1>& bind,
    PairRoleV1 sender,
    const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& receiver_public_key,
    std::array<std::uint8_t, kChannelBindProofBodySizeV1>* out) noexcept;
Status build_channel_bind_proof_hmac_input_v1(
    const std::uint8_t* body, std::size_t size,
    const std::array<std::uint8_t, 32>& exporter,
    std::vector<std::uint8_t>* out);
Status encode_channel_bind_proof_v1(
    const std::array<std::uint8_t, kChannelBindProofBodySizeV1>& body,
    const std::array<std::uint8_t, 32>& tag,
    std::array<std::uint8_t, kChannelBindProofSizeV1>* out) noexcept;
Status decode_channel_bind_proof_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, kChannelBindSizeV1>& expected_bind,
    PairRoleV1 expected_sender,
    const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& receiver_public_key,
    std::array<std::uint8_t, 32>* tag) noexcept;
std::array<std::uint8_t, 32> channel_bind_proof_hash_v1(
    const std::array<std::uint8_t, kChannelBindProofSizeV1>& proof) noexcept;

Status encode_channel_bind_ack_body_v1(
    const std::array<std::uint8_t, 16>& channel_id,
    const std::array<std::uint8_t, 32>& connector_proof_hash,
    const std::array<std::uint8_t, 32>& listener_proof_hash,
    PairRoleV1 connector_role,
    std::array<std::uint8_t, kChannelBindAckBodySizeV1>* out) noexcept;
Status build_channel_bind_ack_hmac_input_v1(
    const std::uint8_t* body, std::size_t size,
    const std::array<std::uint8_t, 32>& exporter,
    std::vector<std::uint8_t>* out);
Status encode_channel_bind_ack_v1(
    const std::array<std::uint8_t, kChannelBindAckBodySizeV1>& body,
    const std::array<std::uint8_t, 32>& tag,
    std::array<std::uint8_t, kChannelBindAckSizeV1>* out) noexcept;
Status decode_channel_bind_ack_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 16>& channel_id,
    const std::array<std::uint8_t, 32>& connector_proof_hash,
    const std::array<std::uint8_t, 32>& listener_proof_hash,
    PairRoleV1 connector_role,
    std::array<std::uint8_t, 32>* tag) noexcept;

std::array<std::uint8_t, 8> bind_stream_preamble_v1() noexcept;

} // namespace flynes::session::wire
#endif
