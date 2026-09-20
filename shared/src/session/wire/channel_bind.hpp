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

/*
 * The ONE canonical channel-bind identity, and the only definition in the tree.
 *
 *   domain_hash("flynes-channel-bind-binding-v1",
 *               channel_id[16] || connector_proof_hash[32] ||
 *               listener_proof_hash[32], 80)
 *
 * The approved 2026-09-04 spec:437 identifies a bind by exactly three
 * authenticated values — channel_id plus the two proof hashes — and both proof
 * hashes are carried inside the 96-byte CHANNEL_BIND_ACK body, so both roles
 * hold them only after each has verified the ACK. This function canonicalises
 * those three values into the single 32-byte value LINK_READY binds as
 * channel_bind_hash; it replaces the u64 channel_bind_id that the first contract
 * revision invented.
 *
 * Both proof hashes MUST be wire::channel_bind_proof_hash_v1(<the exact 248-byte
 * proof>) outputs. Never recompute them by another route: that would be a second
 * definition of the bind identity.
 *
 * Fails closed (Status::InvalidField) on a null output or an all-zero input, so
 * an unwired caller cannot install a zero bind identity.
 */
Status channel_bind_binding_hash_v1(
    const std::array<std::uint8_t, 16>& channel_id,
    const std::array<std::uint8_t, 32>& connector_proof_hash,
    const std::array<std::uint8_t, 32>& listener_proof_hash,
    std::array<std::uint8_t, 32>* out_hash) noexcept;

} // namespace flynes::session::wire
#endif
