#include "pair_secure.hpp"

#include "gatt_fragment.hpp"
#include "pair_capability.hpp"
#include "sha256.hpp"

#include <algorithm>

namespace flynes::session::wire {
namespace {

constexpr std::array<std::uint8_t, 16> kServiceUuid{{
    0xe7, 0x38, 0xdc, 0xda, 0xa2, 0x1a, 0x58, 0x2b,
    0x9c, 0xed, 0xa9, 0x7f, 0x6f, 0xeb, 0xee, 0xdd}};

bool nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return std::any_of(bytes, bytes + size,
                       [](std::uint8_t value) { return value != 0; });
}

bool valid_role(PairRoleV1 role) noexcept
{
    return role == PairRoleV1::Initiator || role == PairRoleV1::Responder;
}

PairRoleV1 opposite(PairRoleV1 role) noexcept
{
    return role == PairRoleV1::Initiator ? PairRoleV1::Responder
                                          : PairRoleV1::Initiator;
}

void put_be32(std::uint8_t* out, std::uint32_t value) noexcept
{
    out[0] = static_cast<std::uint8_t>(value >> 24u);
    out[1] = static_cast<std::uint8_t>(value >> 16u);
    out[2] = static_cast<std::uint8_t>(value >> 8u);
    out[3] = static_cast<std::uint8_t>(value);
}

void put_be64(std::uint8_t* out, std::uint64_t value) noexcept
{
    for (std::size_t index = 0; index < 8; ++index)
        out[index] = static_cast<std::uint8_t>(value >> (56u - index * 8u));
}

std::uint64_t read_be64(const std::uint8_t* bytes) noexcept
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index)
        value = (value << 8u) | bytes[index];
    return value;
}

bool canonical_low_s(const std::array<std::uint8_t, 64>& signature) noexcept
{
    static constexpr std::array<std::uint8_t, 32> half_order{{
        0x7f,0xff,0xff,0xff,0x80,0x00,0x00,0x00,
        0x7f,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xde,0x73,0x7d,0x56,0xd3,0x8b,0xcf,0x42,
        0x79,0xdc,0xe5,0x61,0x7e,0x31,0x92,0xa8}};
    return nonzero(signature.data(), 32) &&
           nonzero(signature.data() + 32, 32) &&
           !std::lexicographical_compare(
               half_order.begin(), half_order.end(),
               signature.begin() + 32, signature.end());
}

bool valid_approval(std::uint8_t entry_mode, PairRoleV1 sender,
                    std::uint8_t approval_kind) noexcept
{
    if (entry_mode == 1)
        return approval_kind == 1 || approval_kind == 4 || approval_kind == 5;
    if (entry_mode == 2)
        return (sender == PairRoleV1::Initiator && approval_kind == 3) ||
               (sender == PairRoleV1::Responder && approval_kind == 2);
    return false;
}

Status build_fixed_hmac_input(const char* domain, std::size_t domain_size,
                              const std::uint8_t* pretag,
                              std::size_t actual_size,
                              std::size_t expected_size,
                              std::vector<std::uint8_t>* out)
{
    if (!domain || !pretag || !out || actual_size != expected_size)
        return Status::InvalidField;
    out->assign(reinterpret_cast<const std::uint8_t*>(domain),
                reinterpret_cast<const std::uint8_t*>(domain) + domain_size);
    const auto offset = out->size();
    out->resize(offset + 4 + actual_size);
    put_be32(out->data() + offset, static_cast<std::uint32_t>(actual_size));
    std::copy_n(pretag, actual_size, out->begin() + offset + 4);
    return Status::Ok;
}

bool valid_plan(const BearerPlanBytes& plan) noexcept
{
    const auto platform = plan[4] == 3 ? std::uint8_t{3} : std::uint8_t{1};
    return validate_bearer_plan_v1(plan, platform) == Status::Ok;
}

} // namespace

Status identity_key_id_v1(
    const std::array<std::uint8_t, 65>& public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, 32>* out) noexcept
{
    if (!out || !validate_point || public_key[0] != 0x04 ||
        !validate_point(validator_context, public_key.data()))
        return Status::InvalidField;
    *out = domain_hash("flynes-identity-key-id-v1", public_key.data(),
                       public_key.size());
    return Status::Ok;
}

Status encode_pair_signature_inner_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    PairRoleV1 sender,
    const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& receiver_public_key,
    const std::array<std::uint8_t, 64>& signature,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, kPairSignatureInnerSizeV1>* out) noexcept
{
    if (!out || !valid_role(sender) ||
        !nonzero(pair_transcript_hash.data(), pair_transcript_hash.size()) ||
        !canonical_low_s(signature))
        return Status::InvalidField;
    std::array<std::uint8_t, 32> sender_id{};
    std::array<std::uint8_t, 32> receiver_id{};
    if (identity_key_id_v1(sender_public_key, validate_point,
                           validator_context, &sender_id) != Status::Ok ||
        identity_key_id_v1(receiver_public_key, validate_point,
                           validator_context, &receiver_id) != Status::Ok)
        return Status::InvalidField;
    out->fill(0);
    (*out)[1] = 1;
    std::copy(pair_transcript_hash.begin(), pair_transcript_hash.end(),
              out->begin() + 8);
    (*out)[40] = static_cast<std::uint8_t>(sender);
    (*out)[41] = static_cast<std::uint8_t>(opposite(sender));
    std::copy(sender_id.begin(), sender_id.end(), out->begin() + 48);
    std::copy(receiver_id.begin(), receiver_id.end(), out->begin() + 80);
    std::copy(signature.begin(), signature.end(), out->begin() + 112);
    return Status::Ok;
}

Status decode_pair_signature_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    PairRoleV1 expected_sender,
    const std::array<std::uint8_t, 65>& expected_sender_public_key,
    const std::array<std::uint8_t, 65>& expected_receiver_public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    PairSignatureInnerV1* out) noexcept
{
    if (!out || !bytes) return Status::InvalidField;
    *out = {};
    if (size < kPairSignatureInnerSizeV1) return Status::Truncated;
    if (size > kPairSignatureInnerSizeV1) return Status::Trailing;
    if (bytes[0] != 0 || bytes[1] != 1) return Status::InvalidField;
    if (nonzero(bytes + 2, 6) || nonzero(bytes + 42, 6))
        return Status::NonzeroReserved;
    if (!valid_role(expected_sender) ||
        bytes[40] != static_cast<std::uint8_t>(expected_sender) ||
        bytes[41] != static_cast<std::uint8_t>(opposite(expected_sender)) ||
        !std::equal(expected_transcript_hash.begin(),
                    expected_transcript_hash.end(), bytes + 8))
        return Status::InvalidField;
    std::array<std::uint8_t, 32> sender_id{};
    std::array<std::uint8_t, 32> receiver_id{};
    if (identity_key_id_v1(expected_sender_public_key, validate_point,
                           validator_context, &sender_id) != Status::Ok ||
        identity_key_id_v1(expected_receiver_public_key, validate_point,
                           validator_context, &receiver_id) != Status::Ok ||
        !std::equal(sender_id.begin(), sender_id.end(), bytes + 48) ||
        !std::equal(receiver_id.begin(), receiver_id.end(), bytes + 80))
        return Status::InvalidField;
    std::array<std::uint8_t, 64> signature{};
    std::copy_n(bytes + 112, signature.size(), signature.begin());
    if (!canonical_low_s(signature)) return Status::InvalidField;
    out->pair_transcript_hash = expected_transcript_hash;
    out->sender = expected_sender;
    out->receiver = opposite(expected_sender);
    out->sender_key_id = sender_id;
    out->receiver_key_id = receiver_id;
    out->signature = signature;
    return Status::Ok;
}

Status encode_key_confirm_inner_v1(
    std::uint8_t entry_mode, std::uint8_t approval_kind,
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    PairRoleV1 sender,
    const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& receiver_public_key,
    const std::array<std::uint8_t, 32>& tag,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, kKeyConfirmInnerSizeV1>* out) noexcept
{
    if (!out || !valid_role(sender) ||
        !valid_approval(entry_mode, sender, approval_kind) ||
        !nonzero(pair_transcript_hash.data(), pair_transcript_hash.size()))
        return Status::InvalidField;
    std::array<std::uint8_t, 32> sender_id{};
    std::array<std::uint8_t, 32> receiver_id{};
    if (identity_key_id_v1(sender_public_key, validate_point,
                           validator_context, &sender_id) != Status::Ok ||
        identity_key_id_v1(receiver_public_key, validate_point,
                           validator_context, &receiver_id) != Status::Ok)
        return Status::InvalidField;
    out->fill(0);
    (*out)[1] = 1;
    (*out)[8] = entry_mode;
    (*out)[9] = approval_kind;
    (*out)[10] = static_cast<std::uint8_t>(sender);
    (*out)[11] = static_cast<std::uint8_t>(opposite(sender));
    std::copy(pair_transcript_hash.begin(), pair_transcript_hash.end(),
              out->begin() + 16);
    std::copy(sender_id.begin(), sender_id.end(), out->begin() + 48);
    std::copy(receiver_id.begin(), receiver_id.end(), out->begin() + 80);
    std::copy(tag.begin(), tag.end(), out->begin() + 112);
    return Status::Ok;
}

Status decode_key_confirm_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    std::uint8_t expected_entry_mode, std::uint8_t expected_approval_kind,
    const std::array<std::uint8_t, 32>& expected_pair_transcript_hash,
    PairRoleV1 expected_sender,
    const std::array<std::uint8_t, 65>& expected_sender_public_key,
    const std::array<std::uint8_t, 65>& expected_receiver_public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    KeyConfirmInnerV1* out) noexcept
{
    if (!out || !bytes) return Status::InvalidField;
    *out = {};
    if (size < kKeyConfirmInnerSizeV1) return Status::Truncated;
    if (size > kKeyConfirmInnerSizeV1) return Status::Trailing;
    if (bytes[0] != 0 || bytes[1] != 1) return Status::InvalidField;
    if (nonzero(bytes + 2, 6) || nonzero(bytes + 12, 4))
        return Status::NonzeroReserved;
    if (!valid_role(expected_sender) || bytes[8] != expected_entry_mode ||
        bytes[9] != expected_approval_kind ||
        bytes[10] != static_cast<std::uint8_t>(expected_sender) ||
        bytes[11] != static_cast<std::uint8_t>(opposite(expected_sender)) ||
        !valid_approval(bytes[8], expected_sender, bytes[9]) ||
        !std::equal(expected_pair_transcript_hash.begin(),
                    expected_pair_transcript_hash.end(), bytes + 16))
        return Status::InvalidField;
    std::array<std::uint8_t, 32> sender_id{};
    std::array<std::uint8_t, 32> receiver_id{};
    if (identity_key_id_v1(expected_sender_public_key, validate_point,
                           validator_context, &sender_id) != Status::Ok ||
        identity_key_id_v1(expected_receiver_public_key, validate_point,
                           validator_context, &receiver_id) != Status::Ok ||
        !std::equal(sender_id.begin(), sender_id.end(), bytes + 48) ||
        !std::equal(receiver_id.begin(), receiver_id.end(), bytes + 80))
        return Status::InvalidField;
    out->entry_mode = bytes[8];
    out->approval_kind = bytes[9];
    out->sender = expected_sender;
    out->receiver = opposite(expected_sender);
    out->pair_transcript_hash = expected_pair_transcript_hash;
    out->sender_key_id = sender_id;
    out->receiver_key_id = receiver_id;
    std::copy_n(bytes + 112, out->tag.size(), out->tag.begin());
    return Status::Ok;
}

Status build_key_confirm_hmac_input_v1(
    const std::uint8_t* body, std::size_t size,
    std::vector<std::uint8_t>* out)
{
    static constexpr char domain[] = "flynes-key-confirm-v1";
    if (!body || !out || size != kKeyConfirmBodySizeV1)
        return Status::InvalidField;
    out->assign(reinterpret_cast<const std::uint8_t*>(domain),
                reinterpret_cast<const std::uint8_t*>(domain) +
                    sizeof(domain) - 1);
    const auto offset = out->size();
    out->resize(offset + 4 + size);
    put_be32(out->data() + offset, static_cast<std::uint32_t>(size));
    std::copy_n(body, size, out->begin() + offset + 4);
    return Status::Ok;
}

Status encode_known_status_inner_v1(
    const std::array<std::uint8_t, 32>& transcript, PairRoleV1 sender,
    bool known, const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& peer_public_key,
    const std::array<std::uint8_t, 32>& tag,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, kKnownStatusInnerSizeV1>* out) noexcept
{
    if (!out || !valid_role(sender) || !nonzero(transcript.data(), transcript.size()))
        return Status::InvalidField;
    std::array<std::uint8_t, 32> sender_id{}, peer_id{};
    if (identity_key_id_v1(sender_public_key, validate_point, validator_context,
                           &sender_id) != Status::Ok ||
        identity_key_id_v1(peer_public_key, validate_point, validator_context,
                           &peer_id) != Status::Ok)
        return Status::InvalidField;
    out->fill(0);
    (*out)[1] = 1;
    std::copy(transcript.begin(), transcript.end(), out->begin() + 8);
    (*out)[40] = static_cast<std::uint8_t>(sender);
    (*out)[41] = static_cast<std::uint8_t>(opposite(sender));
    (*out)[42] = known ? 1 : 0;
    std::copy(sender_id.begin(), sender_id.end(), out->begin() + 48);
    std::copy(peer_id.begin(), peer_id.end(), out->begin() + 80);
    std::copy(tag.begin(), tag.end(), out->begin() + 112);
    return Status::Ok;
}

Status decode_known_status_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& transcript, PairRoleV1 sender,
    const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& peer_public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    KnownStatusInnerV1* out) noexcept
{
    if (!bytes || !out) return Status::InvalidField;
    *out = {};
    if (size < kKnownStatusInnerSizeV1) return Status::Truncated;
    if (size > kKnownStatusInnerSizeV1) return Status::Trailing;
    if (bytes[0] != 0 || bytes[1] != 1 || bytes[42] > 1)
        return Status::InvalidField;
    if (nonzero(bytes + 2, 6) || nonzero(bytes + 43, 5))
        return Status::NonzeroReserved;
    std::array<std::uint8_t, 32> sender_id{}, peer_id{};
    if (!valid_role(sender) || bytes[40] != static_cast<std::uint8_t>(sender) ||
        bytes[41] != static_cast<std::uint8_t>(opposite(sender)) ||
        !std::equal(transcript.begin(), transcript.end(), bytes + 8) ||
        identity_key_id_v1(sender_public_key, validate_point, validator_context,
                           &sender_id) != Status::Ok ||
        identity_key_id_v1(peer_public_key, validate_point, validator_context,
                           &peer_id) != Status::Ok ||
        !std::equal(sender_id.begin(), sender_id.end(), bytes + 48) ||
        !std::equal(peer_id.begin(), peer_id.end(), bytes + 80))
        return Status::InvalidField;
    out->pair_transcript_hash = transcript;
    out->sender = sender;
    out->receiver = opposite(sender);
    out->known = bytes[42] == 1;
    out->sender_key_id = sender_id;
    out->peer_key_id = peer_id;
    std::copy_n(bytes + 112, 32, out->tag.begin());
    return Status::Ok;
}

Status build_known_status_hmac_input_v1(
    const std::uint8_t* body, std::size_t size, std::vector<std::uint8_t>* out)
{
    static constexpr char domain[] = "flynes-known-status-v1";
    return build_fixed_hmac_input(domain, sizeof(domain) - 1, body, size,
                                  kKnownStatusBodySizeV1, out);
}

Status known_branch_digest_v1(
    const std::uint8_t* body, std::size_t size,
    std::array<std::uint8_t, 32>* out) noexcept
{
    if (!body || !out || size != kKnownBranchBodySizeV1)
        return Status::InvalidField;
    *out = domain_hash("flynes-known-branch-v1", body, size);
    return Status::Ok;
}

Status encode_known_branch_inner_v1(
    const std::array<std::uint8_t, 32>& transcript, PairRoleV1 sender,
    KnownBranchKindV1 kind,
    const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& peer_public_key,
    const std::array<std::uint8_t, 64>& proof,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, kKnownBranchInnerSizeV1>* out) noexcept
{
    const bool fallback = kind == KnownBranchKindV1::SasFallback;
    if (!out || !valid_role(sender) ||
        (kind != KnownBranchKindV1::Verified && !fallback) ||
        !nonzero(transcript.data(), transcript.size()) ||
        (fallback ? nonzero(proof.data(), proof.size()) : !canonical_low_s(proof)))
        return Status::InvalidField;
    std::array<std::uint8_t, 32> sender_id{}, peer_id{};
    if (identity_key_id_v1(sender_public_key, validate_point, validator_context,
                           &sender_id) != Status::Ok ||
        identity_key_id_v1(peer_public_key, validate_point, validator_context,
                           &peer_id) != Status::Ok)
        return Status::InvalidField;
    out->fill(0);
    (*out)[1] = 1;
    std::copy(transcript.begin(), transcript.end(), out->begin() + 8);
    (*out)[40] = static_cast<std::uint8_t>(sender);
    (*out)[41] = static_cast<std::uint8_t>(opposite(sender));
    (*out)[42] = static_cast<std::uint8_t>(kind);
    std::copy(sender_id.begin(), sender_id.end(), out->begin() + 48);
    std::copy(peer_id.begin(), peer_id.end(), out->begin() + 80);
    std::copy(proof.begin(), proof.end(), out->begin() + 112);
    return Status::Ok;
}

Status decode_known_branch_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& transcript, PairRoleV1 sender,
    KnownBranchKindV1 expected_kind,
    const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& peer_public_key,
    P256PointValidatorV1 validate_point, void* validator_context,
    KnownBranchInnerV1* out) noexcept
{
    if (!bytes || !out) return Status::InvalidField;
    *out = {};
    if (size < kKnownBranchInnerSizeV1) return Status::Truncated;
    if (size > kKnownBranchInnerSizeV1) return Status::Trailing;
    if (bytes[0] != 0 || bytes[1] != 1 ||
        bytes[42] != static_cast<std::uint8_t>(expected_kind))
        return Status::InvalidField;
    if (nonzero(bytes + 2, 6) || nonzero(bytes + 43, 5))
        return Status::NonzeroReserved;
    std::array<std::uint8_t, 32> sender_id{}, peer_id{};
    std::array<std::uint8_t, 64> proof{};
    std::copy_n(bytes + 112, proof.size(), proof.begin());
    const bool fallback = expected_kind == KnownBranchKindV1::SasFallback;
    if (!valid_role(sender) ||
        (expected_kind != KnownBranchKindV1::Verified && !fallback) ||
        bytes[40] != static_cast<std::uint8_t>(sender) ||
        bytes[41] != static_cast<std::uint8_t>(opposite(sender)) ||
        !std::equal(transcript.begin(), transcript.end(), bytes + 8) ||
        identity_key_id_v1(sender_public_key, validate_point, validator_context,
                           &sender_id) != Status::Ok ||
        identity_key_id_v1(peer_public_key, validate_point, validator_context,
                           &peer_id) != Status::Ok ||
        !std::equal(sender_id.begin(), sender_id.end(), bytes + 48) ||
        !std::equal(peer_id.begin(), peer_id.end(), bytes + 80) ||
        (fallback ? nonzero(proof.data(), proof.size()) : !canonical_low_s(proof)))
        return Status::InvalidField;
    out->pair_transcript_hash = transcript;
    out->sender = sender;
    out->receiver = opposite(sender);
    out->kind = expected_kind;
    out->sender_key_id = sender_id;
    out->peer_key_id = peer_id;
    out->proof = proof;
    return Status::Ok;
}

Status encode_pair_capability_inner_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    PairRoleV1 sender, const std::array<std::uint8_t, 512>& summary,
    const std::array<std::uint8_t, 32>& tag,
    std::array<std::uint8_t, kPairCapabilityInnerSizeV1>* out) noexcept
{
    if (!out || !valid_role(sender) ||
        !nonzero(pair_transcript_hash.data(), pair_transcript_hash.size()) ||
        validate_pair_capability(summary.data(), summary.size()) != Status::Ok)
        return Status::InvalidField;
    out->fill(0);
    (*out)[1] = 1;
    std::copy(pair_transcript_hash.begin(), pair_transcript_hash.end(),
              out->begin() + 8);
    (*out)[40] = static_cast<std::uint8_t>(sender);
    (*out)[41] = static_cast<std::uint8_t>(opposite(sender));
    std::copy(summary.begin(), summary.end(), out->begin() + 48);
    std::copy(tag.begin(), tag.end(), out->begin() + 560);
    return Status::Ok;
}

Status decode_pair_capability_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_pair_transcript_hash,
    PairRoleV1 expected_sender, PairCapabilityInnerV1* out) noexcept
{
    if (!out || !bytes) return Status::InvalidField;
    *out = {};
    if (size < kPairCapabilityInnerSizeV1) return Status::Truncated;
    if (size > kPairCapabilityInnerSizeV1) return Status::Trailing;
    if (bytes[0] != 0 || bytes[1] != 1) return Status::InvalidField;
    if (nonzero(bytes + 2, 6) || nonzero(bytes + 42, 6))
        return Status::NonzeroReserved;
    if (!valid_role(expected_sender) ||
        bytes[40] != static_cast<std::uint8_t>(expected_sender) ||
        bytes[41] != static_cast<std::uint8_t>(opposite(expected_sender)) ||
        !std::equal(expected_pair_transcript_hash.begin(),
                    expected_pair_transcript_hash.end(), bytes + 8))
        return Status::InvalidField;
    std::copy_n(bytes + 48, out->summary.size(), out->summary.begin());
    if (validate_pair_capability(out->summary.data(), out->summary.size()) !=
        Status::Ok)
    {
        *out = {};
        return Status::InvalidField;
    }
    out->pair_transcript_hash = expected_pair_transcript_hash;
    out->sender = expected_sender;
    out->receiver = opposite(expected_sender);
    std::copy_n(bytes + 560, out->tag.size(), out->tag.begin());
    return Status::Ok;
}

Status build_pair_capability_hmac_input_v1(
    const std::uint8_t* pretag, std::size_t size,
    std::vector<std::uint8_t>* out)
{
    static constexpr char domain[] = "flynes-pair-capability-reveal-v1";
    if (!pretag || !out || size != kPairCapabilityPretagSizeV1)
        return Status::InvalidField;
    out->assign(reinterpret_cast<const std::uint8_t*>(domain),
                reinterpret_cast<const std::uint8_t*>(domain) +
                    sizeof(domain) - 1);
    const auto offset = out->size();
    out->resize(offset + 4 + size);
    put_be32(out->data() + offset, static_cast<std::uint32_t>(size));
    std::copy_n(pretag, size, out->begin() + offset + 4);
    return Status::Ok;
}

Status encode_initial_bearer_plan_inner_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 32>& initiator_capability_logical_hash,
    const std::array<std::uint8_t, 32>& responder_capability_logical_hash,
    const BearerPlanBytes& selected_plan,
    const std::array<std::uint8_t, 16>& plan_nonce,
    const std::array<std::uint8_t, 32>& tag,
    std::array<std::uint8_t, kInitialBearerPlanInnerSizeV1>* out) noexcept
{
    if (!out || !nonzero(pair_transcript_hash.data(), 32) ||
        !nonzero(initiator_capability_logical_hash.data(), 32) ||
        !nonzero(responder_capability_logical_hash.data(), 32) ||
        !nonzero(plan_nonce.data(), plan_nonce.size()) || !valid_plan(selected_plan))
        return Status::InvalidField;
    out->fill(0);
    (*out)[1] = 1;
    std::copy(pair_transcript_hash.begin(), pair_transcript_hash.end(),
              out->begin() + 8);
    std::copy(initiator_capability_logical_hash.begin(),
              initiator_capability_logical_hash.end(), out->begin() + 40);
    std::copy(responder_capability_logical_hash.begin(),
              responder_capability_logical_hash.end(), out->begin() + 72);
    std::copy(selected_plan.begin(), selected_plan.end(), out->begin() + 104);
    std::copy(plan_nonce.begin(), plan_nonce.end(), out->begin() + 152);
    (*out)[168] = static_cast<std::uint8_t>(PairRoleV1::Initiator);
    (*out)[169] = static_cast<std::uint8_t>(PairRoleV1::Responder);
    std::copy(tag.begin(), tag.end(), out->begin() + 176);
    return Status::Ok;
}

Status decode_initial_bearer_plan_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    const std::array<std::uint8_t, 32>& expected_initiator_capability_hash,
    const std::array<std::uint8_t, 32>& expected_responder_capability_hash,
    const BearerPlanBytes& expected_plan,
    InitialBearerPlanInnerV1* out) noexcept
{
    if (!bytes || !out) return Status::InvalidField;
    *out = {};
    if (size < kInitialBearerPlanInnerSizeV1) return Status::Truncated;
    if (size > kInitialBearerPlanInnerSizeV1) return Status::Trailing;
    if (bytes[0] != 0 || bytes[1] != 1) return Status::InvalidField;
    if (nonzero(bytes + 2, 6) || nonzero(bytes + 170, 6))
        return Status::NonzeroReserved;
    if (!nonzero(expected_transcript_hash.data(), 32) ||
        !nonzero(expected_initiator_capability_hash.data(), 32) ||
        !nonzero(expected_responder_capability_hash.data(), 32) ||
        !std::equal(expected_transcript_hash.begin(),
                    expected_transcript_hash.end(), bytes + 8) ||
        !std::equal(expected_initiator_capability_hash.begin(),
                    expected_initiator_capability_hash.end(), bytes + 40) ||
        !std::equal(expected_responder_capability_hash.begin(),
                    expected_responder_capability_hash.end(), bytes + 72) ||
        !std::equal(expected_plan.begin(), expected_plan.end(), bytes + 104) ||
        !valid_plan(expected_plan) || !nonzero(bytes + 152, 16) ||
        bytes[168] != static_cast<std::uint8_t>(PairRoleV1::Initiator) ||
        bytes[169] != static_cast<std::uint8_t>(PairRoleV1::Responder))
        return Status::InvalidField;
    out->pair_transcript_hash = expected_transcript_hash;
    out->initiator_capability_logical_hash = expected_initiator_capability_hash;
    out->responder_capability_logical_hash = expected_responder_capability_hash;
    out->selected_plan = expected_plan;
    std::copy_n(bytes + 152, 16, out->plan_nonce.begin());
    out->sender = PairRoleV1::Initiator;
    out->receiver = PairRoleV1::Responder;
    std::copy_n(bytes + 176, 32, out->tag.begin());
    return Status::Ok;
}

Status build_initial_bearer_plan_hmac_input_v1(
    const std::uint8_t* pretag, std::size_t size,
    std::vector<std::uint8_t>* out)
{
    static constexpr char domain[] = "flynes-initial-bearer-plan-v1";
    return build_fixed_hmac_input(domain, sizeof(domain) - 1, pretag, size,
                                  kInitialBearerPlanPretagSizeV1, out);
}

Status encode_initial_bearer_plan_ack_inner_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 32>& plan_logical_hash,
    const std::array<std::uint8_t, 32>& selected_plan_hash,
    const std::array<std::uint8_t, 32>& tag,
    std::array<std::uint8_t, kInitialBearerPlanAckInnerSizeV1>* out) noexcept
{
    if (!out || !nonzero(pair_transcript_hash.data(), 32) ||
        !nonzero(plan_logical_hash.data(), 32) ||
        !nonzero(selected_plan_hash.data(), 32))
        return Status::InvalidField;
    out->fill(0);
    (*out)[1] = 1;
    std::copy(pair_transcript_hash.begin(), pair_transcript_hash.end(),
              out->begin() + 8);
    std::copy(plan_logical_hash.begin(), plan_logical_hash.end(),
              out->begin() + 40);
    std::copy(selected_plan_hash.begin(), selected_plan_hash.end(),
              out->begin() + 72);
    (*out)[104] = static_cast<std::uint8_t>(PairRoleV1::Responder);
    (*out)[105] = static_cast<std::uint8_t>(PairRoleV1::Initiator);
    std::copy(tag.begin(), tag.end(), out->begin() + 112);
    return Status::Ok;
}

Status decode_initial_bearer_plan_ack_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    const std::array<std::uint8_t, 32>& expected_plan_logical_hash,
    const std::array<std::uint8_t, 32>& expected_selected_plan_hash,
    InitialBearerPlanAckInnerV1* out) noexcept
{
    if (!bytes || !out) return Status::InvalidField;
    *out = {};
    if (size < kInitialBearerPlanAckInnerSizeV1) return Status::Truncated;
    if (size > kInitialBearerPlanAckInnerSizeV1) return Status::Trailing;
    if (bytes[0] != 0 || bytes[1] != 1) return Status::InvalidField;
    if (nonzero(bytes + 2, 6) || nonzero(bytes + 106, 6))
        return Status::NonzeroReserved;
    if (!nonzero(expected_transcript_hash.data(), 32) ||
        !nonzero(expected_plan_logical_hash.data(), 32) ||
        !nonzero(expected_selected_plan_hash.data(), 32) ||
        !std::equal(expected_transcript_hash.begin(),
                    expected_transcript_hash.end(), bytes + 8) ||
        !std::equal(expected_plan_logical_hash.begin(),
                    expected_plan_logical_hash.end(), bytes + 40) ||
        !std::equal(expected_selected_plan_hash.begin(),
                    expected_selected_plan_hash.end(), bytes + 72) ||
        bytes[104] != static_cast<std::uint8_t>(PairRoleV1::Responder) ||
        bytes[105] != static_cast<std::uint8_t>(PairRoleV1::Initiator))
        return Status::InvalidField;
    out->pair_transcript_hash = expected_transcript_hash;
    out->plan_logical_hash = expected_plan_logical_hash;
    out->selected_plan_hash = expected_selected_plan_hash;
    out->sender = PairRoleV1::Responder;
    out->receiver = PairRoleV1::Initiator;
    std::copy_n(bytes + 112, 32, out->tag.begin());
    return Status::Ok;
}

Status build_initial_bearer_plan_ack_hmac_input_v1(
    const std::uint8_t* pretag, std::size_t size,
    std::vector<std::uint8_t>* out)
{
    static constexpr char domain[] = "flynes-initial-bearer-plan-ack-v1";
    return build_fixed_hmac_input(domain, sizeof(domain) - 1, pretag, size,
                                  kInitialBearerPlanAckPretagSizeV1, out);
}

Status encode_initial_bearer_plan_final_inner_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 32>& plan_logical_hash,
    const std::array<std::uint8_t, 32>& selected_plan_hash,
    const std::array<std::uint8_t, 32>& plan_ack_logical_hash,
    const std::array<std::uint8_t, 32>& tag,
    std::array<std::uint8_t, kInitialBearerPlanFinalInnerSizeV1>* out) noexcept
{
    if (!out || !nonzero(pair_transcript_hash.data(), 32) ||
        !nonzero(plan_logical_hash.data(), 32) ||
        !nonzero(selected_plan_hash.data(), 32) ||
        !nonzero(plan_ack_logical_hash.data(), 32))
        return Status::InvalidField;
    out->fill(0);
    (*out)[1] = 1;
    std::copy(pair_transcript_hash.begin(), pair_transcript_hash.end(),
              out->begin() + 8);
    std::copy(plan_logical_hash.begin(), plan_logical_hash.end(),
              out->begin() + 40);
    std::copy(selected_plan_hash.begin(), selected_plan_hash.end(),
              out->begin() + 72);
    std::copy(plan_ack_logical_hash.begin(), plan_ack_logical_hash.end(),
              out->begin() + 104);
    (*out)[136] = static_cast<std::uint8_t>(PairRoleV1::Initiator);
    (*out)[137] = static_cast<std::uint8_t>(PairRoleV1::Responder);
    std::copy(tag.begin(), tag.end(), out->begin() + 144);
    return Status::Ok;
}

Status decode_initial_bearer_plan_final_inner_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    const std::array<std::uint8_t, 32>& expected_plan_logical_hash,
    const std::array<std::uint8_t, 32>& expected_selected_plan_hash,
    const std::array<std::uint8_t, 32>& expected_plan_ack_logical_hash,
    InitialBearerPlanFinalInnerV1* out) noexcept
{
    if (!bytes || !out) return Status::InvalidField;
    *out = {};
    if (size < kInitialBearerPlanFinalInnerSizeV1) return Status::Truncated;
    if (size > kInitialBearerPlanFinalInnerSizeV1) return Status::Trailing;
    if (bytes[0] != 0 || bytes[1] != 1) return Status::InvalidField;
    if (nonzero(bytes + 2, 6) || nonzero(bytes + 138, 6))
        return Status::NonzeroReserved;
    if (!nonzero(expected_transcript_hash.data(), 32) ||
        !nonzero(expected_plan_logical_hash.data(), 32) ||
        !nonzero(expected_selected_plan_hash.data(), 32) ||
        !nonzero(expected_plan_ack_logical_hash.data(), 32) ||
        !std::equal(expected_transcript_hash.begin(),
                    expected_transcript_hash.end(), bytes + 8) ||
        !std::equal(expected_plan_logical_hash.begin(),
                    expected_plan_logical_hash.end(), bytes + 40) ||
        !std::equal(expected_selected_plan_hash.begin(),
                    expected_selected_plan_hash.end(), bytes + 72) ||
        !std::equal(expected_plan_ack_logical_hash.begin(),
                    expected_plan_ack_logical_hash.end(), bytes + 104) ||
        bytes[136] != static_cast<std::uint8_t>(PairRoleV1::Initiator) ||
        bytes[137] != static_cast<std::uint8_t>(PairRoleV1::Responder))
        return Status::InvalidField;
    out->pair_transcript_hash = expected_transcript_hash;
    out->plan_logical_hash = expected_plan_logical_hash;
    out->selected_plan_hash = expected_selected_plan_hash;
    out->plan_ack_logical_hash = expected_plan_ack_logical_hash;
    out->sender = PairRoleV1::Initiator;
    out->receiver = PairRoleV1::Responder;
    std::copy_n(bytes + 144, 32, out->tag.begin());
    return Status::Ok;
}

Status build_initial_bearer_plan_final_hmac_input_v1(
    const std::uint8_t* pretag, std::size_t size,
    std::vector<std::uint8_t>* out)
{
    static constexpr char domain[] = "flynes-initial-bearer-plan-final-v1";
    return build_fixed_hmac_input(domain, sizeof(domain) - 1, pretag, size,
                                  kInitialBearerPlanFinalPretagSizeV1, out);
}

Status pair_secure_inner_size_v1(std::uint8_t logical_type,
                                 std::size_t* out) noexcept
{
    if (!out) return Status::InvalidField;
    switch (logical_type)
    {
    case 6: *out = 176; break;
    case 7: *out = 144; break;
    case 17: *out = 176; break;
    case 21: *out = 144; break;
    case 23: *out = 592; break;
    case 24: *out = 208; break;
    case 25: *out = 144; break;
    case 26: *out = 176; break;
    default: *out = 0; return Status::UnknownKind;
    }
    return Status::Ok;
}

Status build_pair_secure_nonce_v1(
    PairRoleV1 sender, std::uint64_t message_counter,
    std::array<std::uint8_t, kPairSecureNonceSizeV1>* out) noexcept
{
    if (!out || !valid_role(sender) || message_counter == 0)
        return Status::InvalidField;
    out->fill(0);
    put_be32(out->data(), sender == PairRoleV1::Initiator
                              ? UINT32_C(0x50324301)
                              : UINT32_C(0x50324302));
    put_be64(out->data() + 4, message_counter);
    return Status::Ok;
}

Status build_pair_secure_aad_v1(
    std::uint8_t logical_type, PairRoleV1 sender,
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    std::uint64_t message_counter,
    std::array<std::uint8_t, kPairSecureAadSizeV1>* out) noexcept
{
    std::size_t inner_size = 0;
    if (!out || !valid_role(sender) || message_counter == 0 ||
        !nonzero(pair_transcript_hash.data(), pair_transcript_hash.size()) ||
        pair_secure_inner_size_v1(logical_type, &inner_size) != Status::Ok)
        return Status::InvalidField;
    out->fill(0);
    std::copy(kServiceUuid.begin(), kServiceUuid.end(), out->begin());
    (*out)[17] = 2;
    (*out)[20] = logical_type;
    (*out)[21] = static_cast<std::uint8_t>(sender);
    (*out)[22] = static_cast<std::uint8_t>(opposite(sender));
    std::copy(pair_transcript_hash.begin(), pair_transcript_hash.end(),
              out->begin() + 24);
    put_be64(out->data() + 56, message_counter);
    put_be32(out->data() + 64, static_cast<std::uint32_t>(inner_size));
    return Status::Ok;
}

Status encode_pair_secure_envelope_v1(
    std::uint8_t logical_type, std::uint64_t message_counter,
    const std::uint8_t* ciphertext_and_tag, std::size_t size,
    std::vector<std::uint8_t>* out)
{
    std::size_t inner_size = 0;
    if (!out || !ciphertext_and_tag || message_counter == 0 ||
        pair_secure_inner_size_v1(logical_type, &inner_size) != Status::Ok ||
        size != inner_size + 16)
        return Status::InvalidField;
    out->assign(12 + size, 0);
    (*out)[0] = 1;
    put_be64(out->data() + 4, message_counter);
    std::copy_n(ciphertext_and_tag, size, out->begin() + 12);
    return Status::Ok;
}

Status decode_pair_secure_envelope_v1(
    std::uint8_t logical_type, const std::uint8_t* bytes, std::size_t size,
    PairSecureEnvelopeV1* out)
{
    if (!out || !bytes) return Status::InvalidField;
    *out = {};
    std::size_t inner_size = 0;
    if (pair_secure_inner_size_v1(logical_type, &inner_size) != Status::Ok)
        return Status::UnknownKind;
    const auto expected = inner_size + 28;
    if (size < expected) return Status::Truncated;
    if (size > expected) return Status::Trailing;
    if (bytes[0] != 1) return Status::InvalidField;
    if (nonzero(bytes + 1, 3)) return Status::NonzeroReserved;
    out->message_counter = read_be64(bytes + 4);
    if (out->message_counter == 0) return Status::InvalidField;
    out->ciphertext_and_tag.assign(bytes + 12, bytes + size);
    return Status::Ok;
}

} // namespace flynes::session::wire
