#ifndef FLYNES_SESSION_WIRE_SESSION_SIGNING_BINDING_HPP
#define FLYNES_SESSION_WIRE_SESSION_SIGNING_BINDING_HPP

#include "pair_handshake.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::session::wire {

constexpr std::size_t kIdentityVerifierRefSizeV1 = 112;
constexpr std::size_t kSessionSigningBindingPretagSizeV1 = 248;
constexpr std::size_t kSessionSigningBindingSizeV1 = 312;
constexpr std::uint16_t kSessionSigningBindingObjectKindV1 = 0x0212;

struct SessionSigningBindingV1 final
{
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    std::array<std::uint8_t, 16> session_id{};
    PairRoleV1 pair_role{};
    std::array<std::uint8_t, 32> identity_key_id{};
    std::array<std::uint8_t, 65> identity_public_key{};
    std::array<std::uint8_t, 65> session_signing_public_key{};
    std::array<std::uint8_t, 64> signature{};
    std::array<std::uint8_t, 32> digest{};
    std::array<std::uint8_t, 32> hash{};
};

Status build_session_signing_binding_pretag_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 16>& session_id, PairRoleV1 pair_role,
    const std::array<std::uint8_t, 65>& identity_public_key,
    const std::array<std::uint8_t, 65>& session_signing_public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, kSessionSigningBindingPretagSizeV1>* out_pretag,
    std::array<std::uint8_t, 32>* out_digest) noexcept;

Status finish_session_signing_binding_v1(
    const std::array<std::uint8_t, kSessionSigningBindingPretagSizeV1>& pretag,
    const std::array<std::uint8_t, 64>& signature,
    std::array<std::uint8_t, kSessionSigningBindingSizeV1>* out_binding,
    std::array<std::uint8_t, 32>* out_hash) noexcept;

Status decode_session_signing_binding_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_pair_transcript_hash,
    const std::array<std::uint8_t, 16>& expected_session_id,
    PairRoleV1 expected_pair_role,
    const std::array<std::uint8_t, 65>& expected_identity_public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    SessionSigningBindingV1* out) noexcept;

} // namespace flynes::session::wire

#endif
