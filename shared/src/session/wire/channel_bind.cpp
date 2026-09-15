#include "channel_bind.hpp"

#include "p256_point.hpp"
#include "sha256.hpp"

#include <algorithm>

namespace flynes::session::wire {
namespace {

bool is_nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return bytes != nullptr &&
           std::any_of(bytes, bytes + size,
                       [](std::uint8_t value) { return value != 0; });
}

bool valid_role(PairRoleV1 role) noexcept
{
    return role == PairRoleV1::Initiator || role == PairRoleV1::Responder;
}

PairRoleV1 opposite_role(PairRoleV1 role) noexcept
{
    return role == PairRoleV1::Initiator
               ? PairRoleV1::Responder
               : PairRoleV1::Initiator;
}

void append_u32be(std::vector<std::uint8_t>* bytes, std::uint32_t value)
{
    bytes->push_back(static_cast<std::uint8_t>(value >> 24));
    bytes->push_back(static_cast<std::uint8_t>(value >> 16));
    bytes->push_back(static_cast<std::uint8_t>(value >> 8));
    bytes->push_back(static_cast<std::uint8_t>(value));
}

Status build_hmac_input(
    const char* domain,
    std::size_t domain_size,
    const std::uint8_t* body,
    std::size_t body_size,
    const std::array<std::uint8_t, 32>& exporter,
    std::vector<std::uint8_t>* out)
{
    if (domain == nullptr || body == nullptr || out == nullptr ||
        !is_nonzero(exporter.data(), exporter.size())) {
        return Status::InvalidField;
    }
    const auto* domain_bytes =
        reinterpret_cast<const std::uint8_t*>(domain);
    out->assign(domain_bytes, domain_bytes + domain_size);
    append_u32be(out, static_cast<std::uint32_t>(body_size));
    out->insert(out->end(), body, body + body_size);
    out->insert(out->end(), exporter.begin(), exporter.end());
    return Status::Ok;
}

} // namespace

Status encode_initial_channel_bind_v1(
    const std::array<std::uint8_t, 32>& transcript,
    const std::array<std::uint8_t, 16>& session,
    const std::array<std::uint8_t, 16>& channel_id,
    std::array<std::uint8_t, kChannelBindSizeV1>* out) noexcept
{
    if (out == nullptr || !is_nonzero(transcript.data(), transcript.size()) ||
        !is_nonzero(session.data(), session.size()) ||
        !is_nonzero(channel_id.data(), channel_id.size())) {
        return Status::InvalidField;
    }

    out->fill(0);
    (*out)[1] = 1;
    (*out)[8] = 1;
    std::copy(transcript.begin(), transcript.end(), out->begin() + 16);
    std::copy(session.begin(), session.end(), out->begin() + 48);
    std::copy(channel_id.begin(), channel_id.end(), out->begin() + 104);

    static constexpr char kDomain[] = "flynes-initial-bind-v1";
    std::array<std::uint8_t, sizeof(kDomain) - 1 + 32> preimage{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(kDomain),
                sizeof(kDomain) - 1, preimage.begin());
    std::copy(transcript.begin(), transcript.end(),
              preimage.begin() + sizeof(kDomain) - 1);
    const auto digest = sha256(preimage.data(), preimage.size());
    std::copy_n(digest.begin(), 16, out->begin() + 120);
    return Status::Ok;
}

Status decode_initial_channel_bind_v1(
    const std::uint8_t* bytes,
    std::size_t size,
    const std::array<std::uint8_t, 32>& transcript,
    const std::array<std::uint8_t, 16>& session,
    const std::array<std::uint8_t, 16>& channel_id) noexcept
{
    if (bytes == nullptr) return Status::InvalidField;
    if (size < kChannelBindSizeV1) return Status::Truncated;
    if (size > kChannelBindSizeV1) return Status::Trailing;

    std::array<std::uint8_t, kChannelBindSizeV1> expected{};
    if (encode_initial_channel_bind_v1(
            transcript, session, channel_id, &expected) != Status::Ok) {
        return Status::InvalidField;
    }
    return std::equal(expected.begin(), expected.end(), bytes)
               ? Status::Ok
               : Status::InvalidField;
}

Status encode_channel_bind_proof_body_v1(
    const std::array<std::uint8_t, kChannelBindSizeV1>& bind,
    PairRoleV1 sender,
    const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& receiver_public_key,
    std::array<std::uint8_t, kChannelBindProofBodySizeV1>* out) noexcept
{
    std::array<std::uint8_t, 32> sender_id{};
    std::array<std::uint8_t, 32> receiver_id{};
    if (out == nullptr || !valid_role(sender) ||
        identity_key_id_v1(sender_public_key,
                           validate_p256_uncompressed_point_callback,
                           nullptr, &sender_id) != Status::Ok ||
        identity_key_id_v1(receiver_public_key,
                           validate_p256_uncompressed_point_callback,
                           nullptr, &receiver_id) != Status::Ok) {
        return Status::InvalidField;
    }

    out->fill(0);
    (*out)[1] = 1;
    std::copy(bind.begin(), bind.end(), out->begin() + 8);
    (*out)[144] = static_cast<std::uint8_t>(sender);
    (*out)[145] = static_cast<std::uint8_t>(opposite_role(sender));
    std::copy(sender_id.begin(), sender_id.end(), out->begin() + 152);
    std::copy(receiver_id.begin(), receiver_id.end(), out->begin() + 184);
    return Status::Ok;
}

Status build_channel_bind_proof_hmac_input_v1(
    const std::uint8_t* body,
    std::size_t size,
    const std::array<std::uint8_t, 32>& exporter,
    std::vector<std::uint8_t>* out)
{
    static constexpr char kDomain[] = "flynes-channel-bind-proof-v1";
    if (size != kChannelBindProofBodySizeV1) return Status::InvalidField;
    return build_hmac_input(kDomain, sizeof(kDomain) - 1, body, size,
                            exporter, out);
}

Status encode_channel_bind_proof_v1(
    const std::array<std::uint8_t, kChannelBindProofBodySizeV1>& body,
    const std::array<std::uint8_t, 32>& tag,
    std::array<std::uint8_t, kChannelBindProofSizeV1>* out) noexcept
{
    if (out == nullptr || !is_nonzero(tag.data(), tag.size())) {
        return Status::InvalidField;
    }
    std::copy(body.begin(), body.end(), out->begin());
    std::copy(tag.begin(), tag.end(),
              out->begin() + kChannelBindProofBodySizeV1);
    return Status::Ok;
}

Status decode_channel_bind_proof_v1(
    const std::uint8_t* bytes,
    std::size_t size,
    const std::array<std::uint8_t, kChannelBindSizeV1>& expected_bind,
    PairRoleV1 expected_sender,
    const std::array<std::uint8_t, 65>& sender_public_key,
    const std::array<std::uint8_t, 65>& receiver_public_key,
    std::array<std::uint8_t, 32>* tag) noexcept
{
    if (bytes == nullptr || tag == nullptr) return Status::InvalidField;
    if (size < kChannelBindProofSizeV1) return Status::Truncated;
    if (size > kChannelBindProofSizeV1) return Status::Trailing;

    std::array<std::uint8_t, kChannelBindProofBodySizeV1> expected{};
    if (encode_channel_bind_proof_body_v1(
            expected_bind, expected_sender, sender_public_key,
            receiver_public_key, &expected) != Status::Ok ||
        !std::equal(expected.begin(), expected.end(), bytes) ||
        !is_nonzero(bytes + kChannelBindProofBodySizeV1, tag->size())) {
        return Status::InvalidField;
    }
    std::copy_n(bytes + kChannelBindProofBodySizeV1, tag->size(),
                tag->begin());
    return Status::Ok;
}

std::array<std::uint8_t, 32> channel_bind_proof_hash_v1(
    const std::array<std::uint8_t, kChannelBindProofSizeV1>& proof) noexcept
{
    static constexpr char kDomain[] = "flynes-channel-bind-proof-hash-v1";
    const auto* domain = reinterpret_cast<const std::uint8_t*>(kDomain);
    std::vector<std::uint8_t> preimage(
        domain, domain + sizeof(kDomain) - 1);
    append_u32be(&preimage,
                 static_cast<std::uint32_t>(kChannelBindProofSizeV1));
    preimage.insert(preimage.end(), proof.begin(), proof.end());
    return sha256(preimage.data(), preimage.size());
}

Status encode_channel_bind_ack_body_v1(
    const std::array<std::uint8_t, 16>& channel_id,
    const std::array<std::uint8_t, 32>& connector_proof_hash,
    const std::array<std::uint8_t, 32>& listener_proof_hash,
    PairRoleV1 connector_role,
    std::array<std::uint8_t, kChannelBindAckBodySizeV1>* out) noexcept
{
    if (out == nullptr || !valid_role(connector_role) ||
        !is_nonzero(channel_id.data(), channel_id.size()) ||
        !is_nonzero(connector_proof_hash.data(), connector_proof_hash.size()) ||
        !is_nonzero(listener_proof_hash.data(), listener_proof_hash.size())) {
        return Status::InvalidField;
    }

    out->fill(0);
    (*out)[1] = 1;
    std::copy(channel_id.begin(), channel_id.end(), out->begin() + 8);
    std::copy(connector_proof_hash.begin(), connector_proof_hash.end(),
              out->begin() + 24);
    std::copy(listener_proof_hash.begin(), listener_proof_hash.end(),
              out->begin() + 56);
    (*out)[88] = static_cast<std::uint8_t>(connector_role);
    (*out)[89] = static_cast<std::uint8_t>(opposite_role(connector_role));
    return Status::Ok;
}

Status build_channel_bind_ack_hmac_input_v1(
    const std::uint8_t* body,
    std::size_t size,
    const std::array<std::uint8_t, 32>& exporter,
    std::vector<std::uint8_t>* out)
{
    static constexpr char kDomain[] = "flynes-channel-bind-ack-v1";
    if (size != kChannelBindAckBodySizeV1) return Status::InvalidField;
    return build_hmac_input(kDomain, sizeof(kDomain) - 1, body, size,
                            exporter, out);
}

Status encode_channel_bind_ack_v1(
    const std::array<std::uint8_t, kChannelBindAckBodySizeV1>& body,
    const std::array<std::uint8_t, 32>& tag,
    std::array<std::uint8_t, kChannelBindAckSizeV1>* out) noexcept
{
    if (out == nullptr || !is_nonzero(tag.data(), tag.size())) {
        return Status::InvalidField;
    }
    std::copy(body.begin(), body.end(), out->begin());
    std::copy(tag.begin(), tag.end(),
              out->begin() + kChannelBindAckBodySizeV1);
    return Status::Ok;
}

Status decode_channel_bind_ack_v1(
    const std::uint8_t* bytes,
    std::size_t size,
    const std::array<std::uint8_t, 16>& channel_id,
    const std::array<std::uint8_t, 32>& connector_proof_hash,
    const std::array<std::uint8_t, 32>& listener_proof_hash,
    PairRoleV1 connector_role,
    std::array<std::uint8_t, 32>* tag) noexcept
{
    if (bytes == nullptr || tag == nullptr) return Status::InvalidField;
    if (size < kChannelBindAckSizeV1) return Status::Truncated;
    if (size > kChannelBindAckSizeV1) return Status::Trailing;

    std::array<std::uint8_t, kChannelBindAckBodySizeV1> expected{};
    if (encode_channel_bind_ack_body_v1(
            channel_id, connector_proof_hash, listener_proof_hash,
            connector_role, &expected) != Status::Ok ||
        !std::equal(expected.begin(), expected.end(), bytes) ||
        !is_nonzero(bytes + kChannelBindAckBodySizeV1, tag->size())) {
        return Status::InvalidField;
    }
    std::copy_n(bytes + kChannelBindAckBodySizeV1, tag->size(), tag->begin());
    return Status::Ok;
}

std::array<std::uint8_t, 8> bind_stream_preamble_v1() noexcept
{
    return {{0x46, 0x4e, 0x42, 0x31, 0x00, 0x01, 0x00, 0x01}};
}

} // namespace flynes::session::wire
