#include "pair_reveal.hpp"

#include <algorithm>

namespace flynes::session::wire {
namespace {

constexpr std::array<std::uint8_t, 16> kFlyNesNearbyServiceUuid{{
    0xe7, 0x38, 0xdc, 0xda, 0xa2, 0x1a, 0x58, 0x2b,
    0x9c, 0xed, 0xa9, 0x7f, 0x6f, 0xeb, 0xee, 0xdd}};

bool nonzero(const std::array<std::uint8_t, 32>& value) noexcept
{
    return std::any_of(value.begin(), value.end(),
                       [](std::uint8_t byte) { return byte != 0; });
}

bool commit_matches(const PairCommitV1& commit, const PairContextV1& context,
                    PairRoleV1 sender, PairRoleV1 receiver) noexcept
{
    return commit.sender == sender && commit.receiver == receiver &&
           commit.pair_context_hash == context.hash &&
           nonzero(commit.commitment);
}

void put_be16(std::uint8_t* out, std::uint16_t value) noexcept
{
    out[0] = static_cast<std::uint8_t>(value >> 8u);
    out[1] = static_cast<std::uint8_t>(value);
}

void put_be32(std::uint8_t* out, std::uint32_t value) noexcept
{
    out[0] = static_cast<std::uint8_t>(value >> 24u);
    out[1] = static_cast<std::uint8_t>(value >> 16u);
    out[2] = static_cast<std::uint8_t>(value >> 8u);
    out[3] = static_cast<std::uint8_t>(value);
}

} // namespace

Status build_pair_reveal_aad_v1(
    const PairContextV1& context, const PairCommitV1& initiator_commit,
    const PairCommitV1& responder_commit, PairRoleV1 sender,
    std::array<std::uint8_t, kPairRevealAadSizeV1>* out) noexcept
{
    if (out == nullptr ||
        (sender != PairRoleV1::Initiator && sender != PairRoleV1::Responder))
        return Status::InvalidField;
    PairContextV1 canonical{};
    if (decode_pair_context_v1(context.bytes.data(), context.bytes.size(),
                               &canonical) != Status::Ok ||
        canonical.hash != context.hash ||
        !commit_matches(initiator_commit, context, PairRoleV1::Initiator,
                        PairRoleV1::Responder) ||
        !commit_matches(responder_commit, context, PairRoleV1::Responder,
                        PairRoleV1::Initiator))
        return Status::InvalidField;

    out->fill(0);
    std::copy(kFlyNesNearbyServiceUuid.begin(),
              kFlyNesNearbyServiceUuid.end(), out->begin());
    put_be16(out->data() + 16, 2);
    put_be16(out->data() + 18, 0);
    (*out)[20] = 5;
    (*out)[21] = static_cast<std::uint8_t>(sender);
    (*out)[22] = static_cast<std::uint8_t>(
        sender == PairRoleV1::Initiator ? PairRoleV1::Responder
                                        : PairRoleV1::Initiator);
    std::copy(context.hash.begin(), context.hash.end(), out->begin() + 24);
    std::copy(initiator_commit.commitment.begin(),
              initiator_commit.commitment.end(), out->begin() + 56);
    std::copy(responder_commit.commitment.begin(),
              responder_commit.commitment.end(), out->begin() + 88);
    put_be32(out->data() + 120,
             static_cast<std::uint32_t>(kPairRevealPlaintextSizeV1));
    return Status::Ok;
}

Status encode_pair_reveal_envelope_v1(
    const std::array<std::uint8_t, 12>& public_nonce,
    const std::array<std::uint8_t, kPairRevealCiphertextAndTagSizeV1>&
        ciphertext_and_tag,
    std::array<std::uint8_t, kPairRevealBodySizeV1>* out) noexcept
{
    if (out == nullptr)
        return Status::InvalidField;
    out->fill(0);
    (*out)[0] = 1;
    std::copy(public_nonce.begin(), public_nonce.end(), out->begin() + 4);
    std::copy(ciphertext_and_tag.begin(), ciphertext_and_tag.end(),
              out->begin() + 16);
    return Status::Ok;
}

Status decode_pair_reveal_envelope_v1(
    const std::uint8_t* bytes, std::size_t size,
    PairRevealEnvelopeV1* out) noexcept
{
    if (out == nullptr || bytes == nullptr)
        return Status::InvalidField;
    *out = {};
    if (size < kPairRevealBodySizeV1)
        return Status::Truncated;
    if (size > kPairRevealBodySizeV1)
        return Status::Trailing;
    if (bytes[0] != 1)
        return Status::InvalidField;
    if (bytes[1] != 0 || bytes[2] != 0 || bytes[3] != 0)
        return Status::NonzeroReserved;
    std::copy_n(bytes + 4, out->public_nonce.size(),
                out->public_nonce.begin());
    std::copy_n(bytes + 16, out->ciphertext_and_tag.size(),
                out->ciphertext_and_tag.begin());
    return Status::Ok;
}

} // namespace flynes::session::wire
