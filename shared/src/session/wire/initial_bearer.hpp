#ifndef FLYNES_SESSION_WIRE_INITIAL_BEARER_HPP
#define FLYNES_SESSION_WIRE_INITIAL_BEARER_HPP

#include "pair_capability.hpp"
#include "pair_handshake.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace flynes::session::wire {

constexpr std::size_t kBearerCredentialBytesSizeV1 = 100;
constexpr std::size_t kBearerJoinParamsSizeV1 = 536;
constexpr std::size_t kInitialBearerCredentialPlaintextSizeV1 = 600;
constexpr std::size_t kInitialBearerCredentialEnvelopeSizeV1 = 628;
constexpr std::size_t kInitialBearerCredentialAadSizeV1 = 84;
constexpr std::size_t kInitialBearerCredentialNonceSizeV1 = 12;

struct BearerJoinParamsV1
{
    std::uint8_t bearer_kind = 0;
    PairRoleV1 bearer_creator{};
    PairRoleV1 quic_listener{};
    std::uint8_t credential_codec = 0;
    std::uint32_t valid_for_ms = 0;
    std::array<std::uint8_t, kBearerCredentialBytesSizeV1> credential{};
};

struct InitialBearerCredentialPlaintextV1
{
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    std::array<std::uint8_t, 16> session_id{};
    PairRoleV1 sender{};
    PairRoleV1 receiver{};
    BearerJoinParamsV1 join{};
};

struct InitialBearerCredentialEnvelopeV1
{
    std::uint64_t message_counter = 0;
    std::vector<std::uint8_t> ciphertext_and_tag;
};

Status validate_bearer_credential_v1(std::uint8_t codec,
                                     const std::uint8_t* bytes,
                                     std::size_t size) noexcept;
Status encode_bearer_join_params_v1(
    const BearerPlanBytes& selected_plan, std::uint32_t valid_for_ms,
    const std::array<std::uint8_t, kBearerCredentialBytesSizeV1>& credential,
    std::array<std::uint8_t, kBearerJoinParamsSizeV1>* out) noexcept;
Status decode_bearer_join_params_v1(
    const std::uint8_t* bytes, std::size_t size,
    const BearerPlanBytes& expected_plan, BearerJoinParamsV1* out) noexcept;

Status encode_initial_bearer_credential_plaintext_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 16>& session_id,
    PairRoleV1 sender, const BearerPlanBytes& selected_plan,
    const std::array<std::uint8_t, kBearerJoinParamsSizeV1>& join_params,
    std::array<std::uint8_t, kInitialBearerCredentialPlaintextSizeV1>* out) noexcept;
Status decode_initial_bearer_credential_plaintext_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    const std::array<std::uint8_t, 16>& expected_session_id,
    PairRoleV1 expected_sender, const BearerPlanBytes& expected_plan,
    InitialBearerCredentialPlaintextV1* out) noexcept;

Status build_initial_bearer_credential_nonce_v1(
    PairRoleV1 sender, std::uint64_t message_counter,
    std::array<std::uint8_t, kInitialBearerCredentialNonceSizeV1>* out) noexcept;
Status build_initial_bearer_credential_aad_v1(
    PairRoleV1 sender,
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 16>& session_id,
    std::uint64_t message_counter,
    std::array<std::uint8_t, kInitialBearerCredentialAadSizeV1>* out) noexcept;
Status encode_initial_bearer_credential_envelope_v1(
    std::uint64_t message_counter, const std::uint8_t* ciphertext_and_tag,
    std::size_t size, std::array<std::uint8_t,
        kInitialBearerCredentialEnvelopeSizeV1>* out) noexcept;
Status decode_initial_bearer_credential_envelope_v1(
    const std::uint8_t* bytes, std::size_t size,
    InitialBearerCredentialEnvelopeV1* out);

} // namespace flynes::session::wire

#endif
