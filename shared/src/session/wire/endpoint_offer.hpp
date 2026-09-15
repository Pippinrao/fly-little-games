#ifndef FLYNES_SESSION_WIRE_ENDPOINT_OFFER_HPP
#define FLYNES_SESSION_WIRE_ENDPOINT_OFFER_HPP

#include "pair_handshake.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace flynes::session::wire {

constexpr std::size_t kEndpointOfferPlaintextSizeV1 = 184;
constexpr std::size_t kEndpointOfferEnvelopeSizeV1 = 216;
constexpr std::size_t kEndpointOfferAadSizeV1 = 108;

enum class EndpointBindingKindV1 : std::uint8_t
{
    Initial = 1,
    NewBearerReconnect = 3
};

enum class EndpointKindV1 : std::uint8_t
{
    Ipv4 = 1,
    Ipv6 = 2,
    AppleBonjourP2p = 3
};

struct EndpointOfferPlaintextV1
{
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    std::array<std::uint8_t, 16> session_id{};
    std::array<std::uint8_t, 32> reconnect_transcript_hash{};
    EndpointBindingKindV1 binding_kind{};
    PairRoleV1 listener{};
    EndpointKindV1 endpoint_kind{};
    std::array<std::uint8_t, 16> endpoint_value{};
    std::uint16_t port = 0;
    std::array<std::uint8_t, 32> bearer_binding_hash{};
    std::array<std::uint8_t, 32> spki_hash{};
};

struct EndpointOfferEnvelopeV1
{
    std::array<std::uint8_t, 12> public_nonce{};
    std::vector<std::uint8_t> ciphertext_and_tag;
};

std::array<std::uint8_t, 32> initial_bearer_binding_hash_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    PairRoleV1 bearer_creator,
    const std::array<std::uint8_t, 32>& credential_logical_hash) noexcept;

Status encode_initial_endpoint_offer_plaintext_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 16>& session_id,
    PairRoleV1 listener, EndpointKindV1 endpoint_kind,
    const std::array<std::uint8_t, 16>& endpoint_value, std::uint16_t port,
    const std::array<std::uint8_t, 32>& bearer_binding_hash,
    const std::array<std::uint8_t, 32>& spki_hash,
    std::array<std::uint8_t, kEndpointOfferPlaintextSizeV1>* out) noexcept;

Status decode_initial_endpoint_offer_plaintext_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_pair_transcript_hash,
    const std::array<std::uint8_t, 16>& expected_session_id,
    PairRoleV1 expected_listener, EndpointKindV1 expected_endpoint_kind,
    const std::array<std::uint8_t, 32>& expected_bearer_binding_hash,
    const std::array<std::uint8_t, 32>& expected_spki_hash,
    EndpointOfferPlaintextV1* out) noexcept;

Status build_endpoint_offer_aad_v1(
    PairRoleV1 sender, EndpointBindingKindV1 binding_kind,
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 16>& session_id,
    const std::array<std::uint8_t, 32>& reconnect_transcript_hash,
    std::array<std::uint8_t, kEndpointOfferAadSizeV1>* out) noexcept;

Status encode_endpoint_offer_envelope_v1(
    const std::array<std::uint8_t, 12>& public_nonce,
    const std::uint8_t* ciphertext_and_tag, std::size_t size,
    std::array<std::uint8_t, kEndpointOfferEnvelopeSizeV1>* out) noexcept;

Status decode_endpoint_offer_envelope_v1(
    const std::uint8_t* bytes, std::size_t size,
    EndpointOfferEnvelopeV1* out);

} // namespace flynes::session::wire

#endif
