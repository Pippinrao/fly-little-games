#ifndef FLYNES_SESSION_WIRE_QUIC_CONTRACT_HPP
#define FLYNES_SESSION_WIRE_QUIC_CONTRACT_HPP

#include "session_codec.hpp"

#include <flynes/flynes_session.h>

#include <array>
#include <cstdint>
#include <string>

namespace flynes::session::wire {

struct QuicHandshakeFactsV2
{
    std::uint16_t tls_major = 0;
    std::uint16_t tls_minor = 0;
    bool full_handshake = false;
    bool pin_verifier_invoked = false;
    bool peer_certificate_verified = false;
    bool resumed = false;
    bool zero_rtt = false;
    std::array<char, 32> alpn{};
    std::array<std::uint8_t, 32> der_spki_hash{};
};

constexpr std::size_t kQuicHandshakeFactsWireSizeV2 = 88;

Status encode_quic_handshake_facts_v2(
    const QuicHandshakeFactsV2& facts,
    std::array<std::uint8_t, kQuicHandshakeFactsWireSizeV2>* out) noexcept;
Status decode_quic_handshake_facts_v2(
    const std::uint8_t* bytes, std::size_t size,
    QuicHandshakeFactsV2* out) noexcept;

struct ChannelExporterRequestV1
{
    std::string label;
    std::array<std::uint8_t, 32> context{};
    std::uint32_t output_size = 0;
};

Status validate_quic_policy_v2(
    const fly_session_quic_connect_policy_v2* policy) noexcept;

Status verify_quic_handshake_v2(
    const fly_session_quic_connect_policy_v2& policy,
    const QuicHandshakeFactsV2& facts) noexcept;

Status verify_quic_listener_handshake_v2(
    const QuicHandshakeFactsV2& facts) noexcept;

ChannelExporterRequestV1 make_channel_exporter_request_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 16>& session_id,
    const std::array<std::uint8_t, 32>& reconnect_transcript_hash);

std::array<std::uint8_t, 16> derive_channel_id_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 16>& session_id,
    const std::array<std::uint8_t, 32>& reconnect_transcript_hash,
    const std::array<std::uint8_t, 32>& exporter);

} // namespace flynes::session::wire

#endif
