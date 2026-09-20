#include "pair_handshake.hpp"
#include "sha256.hpp"

#include <algorithm>

namespace flynes::session::wire {
namespace {

bool all_zero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    for (std::size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != 0)
            return false;
    }
    return true;
}

bool any_nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return !all_zero(bytes, size);
}

std::uint16_t be16(const std::uint8_t* bytes) noexcept
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[0]) << 8u) | bytes[1]);
}

std::uint32_t be32(const std::uint8_t* bytes) noexcept
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24u) |
           (static_cast<std::uint32_t>(bytes[1]) << 16u) |
           (static_cast<std::uint32_t>(bytes[2]) << 8u) |
           bytes[3];
}

void put_be16(std::uint8_t* bytes, std::uint16_t value) noexcept
{
    bytes[0] = static_cast<std::uint8_t>(value >> 8u);
    bytes[1] = static_cast<std::uint8_t>(value);
}

void put_be32(std::uint8_t* bytes, std::uint32_t value) noexcept
{
    bytes[0] = static_cast<std::uint8_t>(value >> 24u);
    bytes[1] = static_cast<std::uint8_t>(value >> 16u);
    bytes[2] = static_cast<std::uint8_t>(value >> 8u);
    bytes[3] = static_cast<std::uint8_t>(value);
}

bool opposite(PairRoleV1 left, PairRoleV1 right) noexcept
{
    return (left == PairRoleV1::Initiator && right == PairRoleV1::Responder) ||
           (left == PairRoleV1::Responder && right == PairRoleV1::Initiator);
}

bool canonical_low_s(const std::array<std::uint8_t, 64>& signature) noexcept
{
    static constexpr std::array<std::uint8_t, 32> half_order{{
        0x7f,0xff,0xff,0xff,0x80,0x00,0x00,0x00,
        0x7f,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xde,0x73,0x7d,0x56,0xd3,0x8b,0xcf,0x42,
        0x79,0xdc,0xe5,0x61,0x7e,0x31,0x92,0xa8}};
    return any_nonzero(signature.data(), 32) &&
           any_nonzero(signature.data() + 32, 32) &&
           !std::lexicographical_compare(
               half_order.begin(), half_order.end(),
               signature.begin() + 32, signature.end());
}

Status exact_size(const std::uint8_t* bytes, std::size_t size,
                  std::size_t expected) noexcept
{
    if (bytes == nullptr)
        return Status::InvalidField;
    if (size < expected)
        return Status::Truncated;
    if (size > expected)
        return Status::Trailing;
    return Status::Ok;
}

} // namespace

Status decode_pair_context_v1(const std::uint8_t* bytes, std::size_t size,
                              PairContextV1* out) noexcept
{
    if (out == nullptr)
        return Status::InvalidField;
    *out = {};
    const auto sized = exact_size(bytes, size, 80);
    if (sized != Status::Ok)
        return sized;
    if (be16(bytes) != 1 || be16(bytes + 8) != 2 || be16(bytes + 10) != 0 ||
        be32(bytes + 48) != 60000)
        return Status::InvalidField;
    if (!all_zero(bytes + 2, 6) || !all_zero(bytes + 13, 3) ||
        !all_zero(bytes + 52, 12))
        return Status::NonzeroReserved;
    if ((bytes[12] != 1 && bytes[12] != 2) ||
        !any_nonzero(bytes + 16, 16) || !any_nonzero(bytes + 32, 16) ||
        !any_nonzero(bytes + 64, 16))
        return Status::InvalidField;
    std::copy_n(bytes, out->bytes.size(), out->bytes.begin());
    out->hash = domain_hash("flynes-pair-context-v1", bytes, size);
    return Status::Ok;
}

Status encode_pair_contribution_v1(
    const PairContextV1& context, PairRoleV1 role,
    const std::array<std::uint8_t, 65>& identity_public_key,
    const std::array<std::uint8_t, 65>& ephemeral_public_key,
    const std::array<std::uint8_t, 32>& tls_spki_hash,
    const std::array<std::uint8_t, 32>& contribution_nonce,
    const std::array<std::uint8_t, 32>& capability_summary_hash,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, 320>* out) noexcept
{
    if (out == nullptr || validate_point == nullptr ||
        (role != PairRoleV1::Initiator && role != PairRoleV1::Responder))
        return Status::InvalidField;
    PairContextV1 canonical_context{};
    if (decode_pair_context_v1(context.bytes.data(), context.bytes.size(),
                               &canonical_context) != Status::Ok ||
        canonical_context.hash != context.hash)
        return Status::InvalidField;

    out->fill(0);
    put_be16(out->data(), 1);
    put_be16(out->data() + 8, 2);
    put_be16(out->data() + 10, 0);
    (*out)[12] = static_cast<std::uint8_t>(role);
    std::copy(context.hash.begin(), context.hash.end(), out->begin() + 16);
    std::copy(context.bytes.begin() + 16, context.bytes.begin() + 48,
              out->begin() + 48);
    put_be32(out->data() + 80, 60000);
    std::copy(identity_public_key.begin(), identity_public_key.end(),
              out->begin() + 88);
    std::copy(ephemeral_public_key.begin(), ephemeral_public_key.end(),
              out->begin() + 153);
    std::copy(tls_spki_hash.begin(), tls_spki_hash.end(), out->begin() + 218);
    std::copy(contribution_nonce.begin(), contribution_nonce.end(),
              out->begin() + 250);
    std::copy(capability_summary_hash.begin(), capability_summary_hash.end(),
              out->begin() + 282);
    PairContributionV1 parsed{};
    const auto status = decode_pair_contribution_v1(
        out->data(), out->size(), context, role, validate_point,
        validator_context, &parsed);
    if (status != Status::Ok)
        out->fill(0);
    return status;
}

Status encode_pair_commit_v1(
    const PairContextV1& context, const PairContributionV1& contribution,
    P256PointValidatorV1 validate_point, void* validator_context,
    std::array<std::uint8_t, 152>* out) noexcept
{
    if (out == nullptr || validate_point == nullptr)
        return Status::InvalidField;
    PairContributionV1 canonical{};
    const auto status = decode_pair_contribution_v1(
        contribution.bytes.data(), contribution.bytes.size(), context,
        contribution.role, validate_point, validator_context, &canonical);
    if (status != Status::Ok || canonical.commitment != contribution.commitment ||
        canonical.identity_public_key != contribution.identity_public_key ||
        canonical.ephemeral_public_key != contribution.ephemeral_public_key ||
        canonical.tls_spki_hash != contribution.tls_spki_hash ||
        canonical.capability_summary_hash != contribution.capability_summary_hash)
        return Status::InvalidField;

    out->fill(0);
    put_be16(out->data(), 1);
    std::copy(context.hash.begin(), context.hash.end(), out->begin() + 8);
    (*out)[40] = static_cast<std::uint8_t>(contribution.role);
    (*out)[41] = static_cast<std::uint8_t>(
        contribution.role == PairRoleV1::Initiator
            ? PairRoleV1::Responder : PairRoleV1::Initiator);
    std::copy(contribution.commitment.begin(), contribution.commitment.end(),
              out->begin() + 48);
    std::copy(contribution.ephemeral_public_key.begin(),
              contribution.ephemeral_public_key.end(), out->begin() + 80);
    return Status::Ok;
}

Status decode_pair_contribution_v1(
    const std::uint8_t* bytes, std::size_t size,
    const PairContextV1& expected_context, PairRoleV1 expected_role,
    P256PointValidatorV1 validate_point, void* validator_context,
    PairContributionV1* out) noexcept
{
    if (out == nullptr || validate_point == nullptr)
        return Status::InvalidField;
    *out = {};
    const auto sized = exact_size(bytes, size, 320);
    if (sized != Status::Ok)
        return sized;
    const auto expected_hash = domain_hash("flynes-pair-context-v1",
                                           expected_context.bytes.data(),
                                           expected_context.bytes.size());
    if (expected_hash != expected_context.hash || be16(bytes) != 1 ||
        be16(bytes + 8) != 2 || be16(bytes + 10) != 0 ||
        bytes[12] != static_cast<std::uint8_t>(expected_role) ||
        be32(bytes + 80) != 60000 ||
        !std::equal(expected_context.hash.begin(), expected_context.hash.end(), bytes + 16) ||
        !std::equal(expected_context.bytes.begin() + 16,
                    expected_context.bytes.begin() + 48, bytes + 48))
        return Status::InvalidField;
    if (!all_zero(bytes + 2, 6) || !all_zero(bytes + 13, 3) ||
        !all_zero(bytes + 84, 4) || !all_zero(bytes + 314, 6))
        return Status::NonzeroReserved;
    if (!any_nonzero(bytes + 218, 32) || !any_nonzero(bytes + 250, 32) ||
        !any_nonzero(bytes + 282, 32) || bytes[88] != 0x04 ||
        bytes[153] != 0x04 ||
        !validate_point(validator_context, bytes + 88) ||
        !validate_point(validator_context, bytes + 153))
        return Status::InvalidField;

    out->role = expected_role;
    std::copy_n(bytes, out->bytes.size(), out->bytes.begin());
    out->commitment = pair_commitment_v1(bytes, size);
    std::copy_n(bytes + 88, 65, out->identity_public_key.begin());
    std::copy_n(bytes + 153, 65, out->ephemeral_public_key.begin());
    std::copy_n(bytes + 218, 32, out->tls_spki_hash.begin());
    std::copy_n(bytes + 282, 32, out->capability_summary_hash.begin());
    return Status::Ok;
}

Status decode_pair_commit_v1(
    const std::uint8_t* bytes, std::size_t size,
    const PairContextV1& expected_context, PairRoleV1 expected_sender,
    P256PointValidatorV1 validate_point, void* validator_context,
    PairCommitV1* out) noexcept
{
    if (out == nullptr || validate_point == nullptr)
        return Status::InvalidField;
    *out = {};
    const auto sized = exact_size(bytes, size, 152);
    if (sized != Status::Ok)
        return sized;
    const auto receiver = static_cast<PairRoleV1>(bytes[41]);
    if (be16(bytes) != 1 || bytes[40] != static_cast<std::uint8_t>(expected_sender) ||
        !opposite(expected_sender, receiver) ||
        !std::equal(expected_context.hash.begin(), expected_context.hash.end(), bytes + 8) ||
        !any_nonzero(bytes + 48, 32) || bytes[80] != 0x04 ||
        !validate_point(validator_context, bytes + 80))
        return Status::InvalidField;
    if (!all_zero(bytes + 2, 6) || !all_zero(bytes + 42, 6) ||
        !all_zero(bytes + 145, 7))
        return Status::NonzeroReserved;
    out->sender = expected_sender;
    out->receiver = receiver;
    out->pair_context_hash = expected_context.hash;
    std::copy_n(bytes + 48, 32, out->commitment.begin());
    std::copy_n(bytes + 80, 65, out->ephemeral_public_key.begin());
    return Status::Ok;
}

Status bind_reveal_to_commit_v1(const PairCommitV1& commit,
                                const PairContributionV1& reveal) noexcept
{
    const auto calculated = pair_commitment_v1(reveal.bytes.data(), reveal.bytes.size());
    if (commit.sender != reveal.role || reveal.commitment != calculated ||
        commit.commitment != calculated ||
        !std::equal(reveal.bytes.begin() + 153, reveal.bytes.begin() + 218,
                    reveal.ephemeral_public_key.begin()) ||
        commit.ephemeral_public_key != reveal.ephemeral_public_key)
        return Status::InvalidField;
    return Status::Ok;
}

PairExchangeV1::PairExchangeV1(P256PointValidatorV1 validate_point,
                               void* validator_context) noexcept
    : validate_point_(validate_point), validator_context_(validator_context)
{
}

PairExchangeResultV1 PairExchangeV1::reject() noexcept
{
    phase_ = Phase::Failed;
    return PairExchangeResultV1::ProtocolViolation;
}

bool PairExchangeV1::nonzero(
    const std::array<std::uint8_t, 32>& value) noexcept
{
    return std::any_of(value.begin(), value.end(),
                       [](std::uint8_t byte) { return byte != 0; });
}

PairExchangeResultV1 PairExchangeV1::begin(
    const std::uint8_t* bytes, std::size_t size) noexcept
{
    if (phase_ != Phase::Empty)
        return reject();
    if (validate_point_ == nullptr ||
        decode_pair_context_v1(bytes, size, &context_) != Status::Ok)
        return reject();
    phase_ = Phase::InitiatorCommit;
    return PairExchangeResultV1::Accepted;
}

PairExchangeResultV1 PairExchangeV1::accept_commit(
    PairRoleV1 sender, const std::uint8_t* bytes, std::size_t size) noexcept
{
    const bool initiator = phase_ == Phase::InitiatorCommit &&
        sender == PairRoleV1::Initiator;
    const bool responder = phase_ == Phase::ResponderCommit &&
        sender == PairRoleV1::Responder;
    if (!initiator && !responder)
        return reject();
    const std::size_t index = initiator ? 0u : 1u;
    if (decode_pair_commit_v1(bytes, size, context_, sender, validate_point_,
                              validator_context_, &commits_[index]) != Status::Ok)
        return reject();
    phase_ = initiator ? Phase::ResponderCommit : Phase::InitiatorReveal;
    return PairExchangeResultV1::Accepted;
}

PairExchangeResultV1 PairExchangeV1::accept_reveal(
    PairRoleV1 sender, const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& logical_hash) noexcept
{
    const bool initiator = phase_ == Phase::InitiatorReveal &&
        sender == PairRoleV1::Initiator;
    const bool responder = phase_ == Phase::ResponderReveal &&
        sender == PairRoleV1::Responder;
    if ((!initiator && !responder) || !nonzero(logical_hash))
        return reject();
    const std::size_t index = initiator ? 0u : 1u;
    if (decode_pair_contribution_v1(
            bytes, size, context_, sender, validate_point_, validator_context_,
            &contributions_[index]) != Status::Ok ||
        bind_reveal_to_commit_v1(commits_[index], contributions_[index]) !=
            Status::Ok)
        return reject();
    reveal_hashes_[index] = logical_hash;
    if (initiator)
    {
        phase_ = Phase::ResponderReveal;
        return PairExchangeResultV1::Accepted;
    }
    phase_ = Phase::Ready;
    return PairExchangeResultV1::Ready;
}

Status build_pair_transcript_preimage_v1(
    std::uint8_t entry_mode,
    const std::array<std::uint8_t, 32>& entry_context_hash,
    const std::array<std::uint8_t, 32>& initiator_commitment,
    const std::array<std::uint8_t, 32>& responder_commitment,
    const PairContributionV1& initiator,
    const PairContributionV1& responder,
    std::array<std::uint8_t, 752>* out_preimage,
    std::array<std::uint8_t, 32>* out_hash) noexcept
{
    if (out_preimage == nullptr || out_hash == nullptr ||
        (entry_mode != 1 && entry_mode != 2) ||
        (entry_mode == 1 && any_nonzero(entry_context_hash.data(), entry_context_hash.size())) ||
        (entry_mode == 2 && !any_nonzero(entry_context_hash.data(), entry_context_hash.size())) ||
        initiator.role != PairRoleV1::Initiator ||
        responder.role != PairRoleV1::Responder ||
        initiator.commitment != initiator_commitment ||
        responder.commitment != responder_commitment)
        return Status::InvalidField;
    out_preimage->fill(0);
    (*out_preimage)[1] = 1;
    (*out_preimage)[8] = entry_mode;
    std::copy(entry_context_hash.begin(), entry_context_hash.end(),
              out_preimage->begin() + 16);
    std::copy(initiator_commitment.begin(), initiator_commitment.end(),
              out_preimage->begin() + 48);
    std::copy(responder_commitment.begin(), responder_commitment.end(),
              out_preimage->begin() + 80);
    std::copy(initiator.bytes.begin(), initiator.bytes.end(),
              out_preimage->begin() + 112);
    std::copy(responder.bytes.begin(), responder.bytes.end(),
              out_preimage->begin() + 432);
    *out_hash = domain_hash("flynes-pair-transcript-v1",
                            out_preimage->data(), out_preimage->size());
    return Status::Ok;
}

Status build_pair_transcript_object_v1(
    const std::array<std::uint8_t, kPairTranscriptPreimageSizeV1>& preimage,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    const std::array<std::uint8_t, 64>& initiator_signature,
    const std::array<std::uint8_t, 64>& responder_signature,
    std::array<std::uint8_t, kPairTranscriptSizeV1>* out_transcript,
    std::array<std::uint8_t, 32>* out_object_hash) noexcept
{
    if (out_transcript == nullptr || out_object_hash == nullptr ||
        domain_hash("flynes-pair-transcript-v1", preimage.data(),
                    preimage.size()) != expected_transcript_hash ||
        !canonical_low_s(initiator_signature) ||
        !canonical_low_s(responder_signature))
        return Status::InvalidField;
    std::copy(preimage.begin(), preimage.end(), out_transcript->begin());
    std::copy(initiator_signature.begin(), initiator_signature.end(),
              out_transcript->begin() + kPairTranscriptPreimageSizeV1);
    std::copy(responder_signature.begin(), responder_signature.end(),
              out_transcript->begin() + kPairTranscriptPreimageSizeV1 + 64);
    *out_object_hash = domain_hash(
        "flynes-pair-transcript-object-v1", out_transcript->data(),
        out_transcript->size());
    return Status::Ok;
}

} // namespace flynes::session::wire
