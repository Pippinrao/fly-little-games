#include "endpoint_offer.hpp"

#include "sha256.hpp"

#include <algorithm>

namespace flynes::session::wire {
namespace {

constexpr std::array<std::uint8_t, 16> kServiceUuid{{
    0xe7,0x38,0xdc,0xda,0xa2,0x1a,0x58,0x2b,
    0x9c,0xed,0xa9,0x7f,0x6f,0xeb,0xee,0xdd}};

bool nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return bytes && std::any_of(bytes, bytes + size,
        [](std::uint8_t value) { return value != 0; });
}

bool zero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return !nonzero(bytes, size);
}

bool role(PairRoleV1 value) noexcept
{
    return value == PairRoleV1::Initiator || value == PairRoleV1::Responder;
}

PairRoleV1 opposite(PairRoleV1 value) noexcept
{
    return value == PairRoleV1::Initiator ? PairRoleV1::Responder
                                          : PairRoleV1::Initiator;
}

void put_be16(std::uint8_t* out, std::uint16_t value) noexcept
{
    out[0] = static_cast<std::uint8_t>(value >> 8u);
    out[1] = static_cast<std::uint8_t>(value);
}

std::uint16_t read_be16(const std::uint8_t* bytes) noexcept
{
    return static_cast<std::uint16_t>((bytes[0] << 8u) | bytes[1]);
}

bool valid_endpoint(EndpointKindV1 kind,
                    const std::array<std::uint8_t, 16>& value,
                    std::uint16_t port) noexcept
{
    if (kind == EndpointKindV1::Ipv4)
        return zero(value.data(), 12) && nonzero(value.data() + 12, 4) &&
               port >= 49152;
    if (kind == EndpointKindV1::Ipv6)
        return nonzero(value.data(), value.size()) && port >= 49152;
    if (kind == EndpointKindV1::AppleBonjourP2p)
        return nonzero(value.data(), value.size()) && port == 0;
    return false;
}

} // namespace

std::array<std::uint8_t, 32> initial_bearer_binding_hash_v1(
    const std::array<std::uint8_t, 32>& transcript, PairRoleV1 creator,
    const std::array<std::uint8_t, 32>& credential_hash) noexcept
{
    static constexpr char domain[] = "flynes-initial-bearer-binding-v1";
    std::array<std::uint8_t, sizeof(domain) - 1 + 32 + 1 + 32> input{};
    std::copy_n(reinterpret_cast<const std::uint8_t*>(domain),
                sizeof(domain) - 1, input.begin());
    auto at = input.begin() + sizeof(domain) - 1;
    at = std::copy(transcript.begin(), transcript.end(), at);
    *at++ = static_cast<std::uint8_t>(creator);
    std::copy(credential_hash.begin(), credential_hash.end(), at);
    return sha256(input.data(), input.size());
}

Status encode_initial_endpoint_offer_plaintext_v1(
    const std::array<std::uint8_t, 32>& transcript,
    const std::array<std::uint8_t, 16>& session, PairRoleV1 listener,
    EndpointKindV1 endpoint_kind,
    const std::array<std::uint8_t, 16>& endpoint_value, std::uint16_t port,
    const std::array<std::uint8_t, 32>& binding,
    const std::array<std::uint8_t, 32>& spki,
    std::array<std::uint8_t, kEndpointOfferPlaintextSizeV1>* out) noexcept
{
    if (!out || !role(listener) || !nonzero(transcript.data(), transcript.size()) ||
        !nonzero(session.data(), session.size()) ||
        !nonzero(binding.data(), binding.size()) ||
        !nonzero(spki.data(), spki.size()) ||
        !valid_endpoint(endpoint_kind, endpoint_value, port))
        return Status::InvalidField;
    out->fill(0);
    (*out)[1] = 1;
    std::copy(transcript.begin(), transcript.end(), out->begin() + 8);
    std::copy(session.begin(), session.end(), out->begin() + 40);
    (*out)[88] = static_cast<std::uint8_t>(EndpointBindingKindV1::Initial);
    (*out)[89] = static_cast<std::uint8_t>(listener);
    (*out)[90] = static_cast<std::uint8_t>(endpoint_kind);
    std::copy(endpoint_value.begin(), endpoint_value.end(), out->begin() + 96);
    put_be16(out->data() + 112, port);
    std::copy(binding.begin(), binding.end(), out->begin() + 120);
    std::copy(spki.begin(), spki.end(), out->begin() + 152);
    return Status::Ok;
}

Status decode_initial_endpoint_offer_plaintext_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& transcript,
    const std::array<std::uint8_t, 16>& session,
    PairRoleV1 listener, EndpointKindV1 endpoint_kind,
    const std::array<std::uint8_t, 32>& binding,
    const std::array<std::uint8_t, 32>& spki,
    EndpointOfferPlaintextV1* out) noexcept
{
    if (!bytes || !out) return Status::InvalidField;
    *out = {};
    if (size < kEndpointOfferPlaintextSizeV1) return Status::Truncated;
    if (size > kEndpointOfferPlaintextSizeV1) return Status::Trailing;
    if (bytes[0] != 0 || bytes[1] != 1 ||
        bytes[88] != static_cast<std::uint8_t>(EndpointBindingKindV1::Initial) ||
        bytes[89] != static_cast<std::uint8_t>(listener) ||
        bytes[90] != static_cast<std::uint8_t>(endpoint_kind) ||
        !std::equal(transcript.begin(), transcript.end(), bytes + 8) ||
        !std::equal(session.begin(), session.end(), bytes + 40) ||
        !zero(bytes + 56, 32) || !zero(bytes + 2, 6) ||
        !zero(bytes + 91, 5) || !zero(bytes + 114, 6) ||
        !std::equal(binding.begin(), binding.end(), bytes + 120) ||
        !std::equal(spki.begin(), spki.end(), bytes + 152))
        return Status::InvalidField;
    std::array<std::uint8_t, 16> endpoint{};
    std::copy_n(bytes + 96, endpoint.size(), endpoint.begin());
    const auto port = read_be16(bytes + 112);
    if (!valid_endpoint(endpoint_kind, endpoint, port))
        return Status::InvalidField;
    out->pair_transcript_hash = transcript;
    out->session_id = session;
    out->binding_kind = EndpointBindingKindV1::Initial;
    out->listener = listener;
    out->endpoint_kind = endpoint_kind;
    out->endpoint_value = endpoint;
    out->port = port;
    out->bearer_binding_hash = binding;
    out->spki_hash = spki;
    return Status::Ok;
}

Status build_endpoint_offer_aad_v1(
    PairRoleV1 sender, EndpointBindingKindV1 binding_kind,
    const std::array<std::uint8_t, 32>& transcript,
    const std::array<std::uint8_t, 16>& session,
    const std::array<std::uint8_t, 32>& reconnect,
    std::array<std::uint8_t, kEndpointOfferAadSizeV1>* out) noexcept
{
    if (!out || !role(sender) ||
        (binding_kind != EndpointBindingKindV1::Initial &&
         binding_kind != EndpointBindingKindV1::NewBearerReconnect) ||
        !nonzero(transcript.data(), transcript.size()) ||
        !nonzero(session.data(), session.size()) ||
        (binding_kind == EndpointBindingKindV1::Initial
             ? !zero(reconnect.data(), reconnect.size())
             : !nonzero(reconnect.data(), reconnect.size())))
        return Status::InvalidField;
    out->fill(0);
    std::copy(kServiceUuid.begin(), kServiceUuid.end(), out->begin());
    (*out)[17] = 2;
    (*out)[19] = 1;
    (*out)[20] = 22;
    (*out)[21] = static_cast<std::uint8_t>(sender);
    (*out)[22] = static_cast<std::uint8_t>(opposite(sender));
    (*out)[23] = static_cast<std::uint8_t>(binding_kind);
    std::copy(transcript.begin(), transcript.end(), out->begin() + 24);
    std::copy(session.begin(), session.end(), out->begin() + 56);
    std::copy(reconnect.begin(), reconnect.end(), out->begin() + 72);
    (*out)[107] = static_cast<std::uint8_t>(kEndpointOfferPlaintextSizeV1);
    return Status::Ok;
}

Status encode_endpoint_offer_envelope_v1(
    const std::array<std::uint8_t, 12>& nonce,
    const std::uint8_t* ciphertext, std::size_t size,
    std::array<std::uint8_t, kEndpointOfferEnvelopeSizeV1>* out) noexcept
{
    if (!out || !ciphertext || size != kEndpointOfferPlaintextSizeV1 + 16 ||
        !nonzero(nonce.data(), nonce.size()))
        return Status::InvalidField;
    out->fill(0);
    (*out)[0] = 1;
    std::copy(nonce.begin(), nonce.end(), out->begin() + 4);
    std::copy_n(ciphertext, size, out->begin() + 16);
    return Status::Ok;
}

Status decode_endpoint_offer_envelope_v1(
    const std::uint8_t* bytes, std::size_t size, EndpointOfferEnvelopeV1* out)
{
    if (!bytes || !out) return Status::InvalidField;
    *out = {};
    if (size < kEndpointOfferEnvelopeSizeV1) return Status::Truncated;
    if (size > kEndpointOfferEnvelopeSizeV1) return Status::Trailing;
    if (bytes[0] != 1 || !zero(bytes + 1, 3) || !nonzero(bytes + 4, 12))
        return Status::InvalidField;
    std::copy_n(bytes + 4, out->public_nonce.size(), out->public_nonce.begin());
    out->ciphertext_and_tag.assign(bytes + 16, bytes + size);
    return Status::Ok;
}

} // namespace flynes::session::wire
