#include "session_signing_binding.hpp"

#include "sha256.hpp"

#include <algorithm>
#include <cstring>

namespace flynes::session::wire {
namespace {

bool nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return bytes && std::any_of(bytes, bytes + size,
        [](std::uint8_t value) { return value != 0; });
}

bool zeros(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return !nonzero(bytes, size);
}

bool valid_role(PairRoleV1 role) noexcept
{
    return role == PairRoleV1::Initiator || role == PairRoleV1::Responder;
}

bool canonical_signature(const std::uint8_t signature[64]) noexcept
{
    static constexpr std::array<std::uint8_t, 32> order{{
        0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
        0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,
        0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51}};
    static constexpr std::array<std::uint8_t, 32> half_order{{
        0x7f,0xff,0xff,0xff,0x80,0x00,0x00,0x00,
        0x7f,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xde,0x73,0x7d,0x56,0xd3,0x8b,0xcf,0x42,
        0x79,0xdc,0xe5,0x61,0x7e,0x31,0x92,0xa8}};
    return nonzero(signature, 32) &&
           std::memcmp(signature, order.data(), 32) < 0 &&
           nonzero(signature + 32, 32) &&
           std::memcmp(signature + 32, half_order.data(), 32) <= 0;
}

Status validate_pretag(
    const std::uint8_t* bytes, P256PointValidatorV1 validate_point,
    void* validator_context) noexcept
{
    if (!bytes || !validate_point) return Status::InvalidField;
    if (bytes[0] != 0 || bytes[1] != 1 || bytes[64] != 0 ||
        bytes[65] != 1)
        return Status::InvalidField;
    if (!zeros(bytes + 2, 6) || !zeros(bytes + 57, 7) ||
        !zeros(bytes + 66, 6) || !zeros(bytes + 169, 7) ||
        !zeros(bytes + 241, 7))
        return Status::NonzeroReserved;
    if (!nonzero(bytes + 8, 32) || !nonzero(bytes + 40, 16) ||
        (bytes[56] != 1 && bytes[56] != 2) ||
        !validate_point(validator_context, bytes + 104) ||
        !validate_point(validator_context, bytes + 176) ||
        std::memcmp(bytes + 104, bytes + 176, 65) == 0)
        return Status::InvalidField;
    const auto key_id = domain_hash(
        "flynes-identity-key-id-v1", bytes + 104, 65);
    return std::memcmp(bytes + 72, key_id.data(), key_id.size()) == 0
        ? Status::Ok : Status::InvalidField;
}

} // namespace

Status build_session_signing_binding_pretag_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 16>& session_id, PairRoleV1 pair_role,
    const std::array<std::uint8_t, 65>& identity_public_key,
    const std::array<std::uint8_t, 65>& session_signing_public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, kSessionSigningBindingPretagSizeV1>* out_pretag,
    std::array<std::uint8_t, 32>* out_digest) noexcept
{
    if (!out_pretag || !out_digest || !valid_role(pair_role) ||
        !nonzero(pair_transcript_hash.data(), pair_transcript_hash.size()) ||
        !nonzero(session_id.data(), session_id.size()) || !validate_point ||
        !validate_point(validator_context, identity_public_key.data()) ||
        !validate_point(validator_context, session_signing_public_key.data()) ||
        identity_public_key == session_signing_public_key)
        return Status::InvalidField;
    out_pretag->fill(0);
    (*out_pretag)[1] = 1;
    std::copy(pair_transcript_hash.begin(), pair_transcript_hash.end(),
              out_pretag->begin() + 8);
    std::copy(session_id.begin(), session_id.end(), out_pretag->begin() + 40);
    (*out_pretag)[56] = static_cast<std::uint8_t>(pair_role);
    (*out_pretag)[65] = 1;
    const auto key_id = domain_hash(
        "flynes-identity-key-id-v1", identity_public_key.data(),
        identity_public_key.size());
    std::copy(key_id.begin(), key_id.end(), out_pretag->begin() + 72);
    std::copy(identity_public_key.begin(), identity_public_key.end(),
              out_pretag->begin() + 104);
    std::copy(session_signing_public_key.begin(),
              session_signing_public_key.end(), out_pretag->begin() + 176);
    *out_digest = domain_hash("flynes-session-signing-key-binding-v1",
                              out_pretag->data(), out_pretag->size());
    return Status::Ok;
}

Status finish_session_signing_binding_v1(
    const std::array<std::uint8_t, kSessionSigningBindingPretagSizeV1>& pretag,
    const std::array<std::uint8_t, 64>& signature,
    std::array<std::uint8_t, kSessionSigningBindingSizeV1>* out_binding,
    std::array<std::uint8_t, 32>* out_hash) noexcept
{
    if (!out_binding || !out_hash || !canonical_signature(signature.data()))
        return Status::InvalidField;
    std::copy(pretag.begin(), pretag.end(), out_binding->begin());
    std::copy(signature.begin(), signature.end(), out_binding->begin() + 248);
    *out_hash = domain_hash("flynes-session-signing-key-binding-hash-v1",
                            out_binding->data(), out_binding->size());
    return Status::Ok;
}

Status decode_session_signing_binding_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_pair_transcript_hash,
    const std::array<std::uint8_t, 16>& expected_session_id,
    PairRoleV1 expected_pair_role,
    const std::array<std::uint8_t, 65>& expected_identity_public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    SessionSigningBindingV1* out) noexcept
{
    if (size < kSessionSigningBindingSizeV1) return Status::Truncated;
    if (size > kSessionSigningBindingSizeV1) return Status::Trailing;
    if (!bytes || !out || !valid_role(expected_pair_role))
        return Status::InvalidField;
    const auto pretag_status = validate_pretag(
        bytes, validate_point, validator_context);
    if (pretag_status != Status::Ok) return pretag_status;
    if (std::memcmp(bytes + 8, expected_pair_transcript_hash.data(), 32) != 0 ||
        std::memcmp(bytes + 40, expected_session_id.data(), 16) != 0 ||
        bytes[56] != static_cast<std::uint8_t>(expected_pair_role) ||
        std::memcmp(bytes + 104, expected_identity_public_key.data(), 65) != 0 ||
        !canonical_signature(bytes + 248))
        return Status::InvalidField;
    SessionSigningBindingV1 parsed{};
    std::copy_n(bytes + 8, 32, parsed.pair_transcript_hash.begin());
    std::copy_n(bytes + 40, 16, parsed.session_id.begin());
    parsed.pair_role = expected_pair_role;
    std::copy_n(bytes + 72, 32, parsed.identity_key_id.begin());
    std::copy_n(bytes + 104, 65, parsed.identity_public_key.begin());
    std::copy_n(bytes + 176, 65,
                parsed.session_signing_public_key.begin());
    std::copy_n(bytes + 248, 64, parsed.signature.begin());
    parsed.digest = domain_hash("flynes-session-signing-key-binding-v1",
                                bytes, 248);
    parsed.hash = domain_hash("flynes-session-signing-key-binding-hash-v1",
                              bytes, 312);
    *out = parsed;
    return Status::Ok;
}

} // namespace flynes::session::wire
