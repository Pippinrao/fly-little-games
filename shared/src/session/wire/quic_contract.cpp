#include "quic_contract.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace flynes::session::wire {
namespace {

constexpr char kAlpn[] = "flynes-nearby/2";
constexpr char kExporterLabel[] = "EXPORTER-flynes-nearby-v1";

bool any_nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    for (std::size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != 0)
            return true;
    }
    return false;
}

bool exact_text(const char* bytes, std::size_t size, const char* expected,
                std::size_t expected_size) noexcept
{
    if (expected_size + 1 > size || std::memcmp(bytes, expected, expected_size) != 0 ||
        bytes[expected_size] != '\0')
        return false;
    for (std::size_t i = expected_size + 1; i < size; ++i)
    {
        if (bytes[i] != '\0')
            return false;
    }
    return true;
}

template <std::size_t N1, std::size_t N2, std::size_t N3>
std::array<std::uint8_t, 32> concat_hash(
    const char* domain, const std::array<std::uint8_t, N1>& first,
    const std::array<std::uint8_t, N2>& second,
    const std::array<std::uint8_t, N3>& third)
{
    const std::size_t domain_size = std::strlen(domain);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(domain_size + N1 + N2 + N3);
    bytes.insert(bytes.end(), domain, domain + domain_size);
    bytes.insert(bytes.end(), first.begin(), first.end());
    bytes.insert(bytes.end(), second.begin(), second.end());
    bytes.insert(bytes.end(), third.begin(), third.end());
    return sha256(bytes.data(), bytes.size());
}

} // namespace

Status encode_quic_handshake_facts_v2(
    const QuicHandshakeFactsV2& facts,
    std::array<std::uint8_t, kQuicHandshakeFactsWireSizeV2>* out) noexcept
{
    if (!out || facts.tls_major != 1 || facts.tls_minor != 3 ||
        !exact_text(facts.alpn.data(), facts.alpn.size(), kAlpn,
                    sizeof(kAlpn) - 1))
        return Status::InvalidField;
    out->fill(0);
    (*out)[1] = 1;
    (*out)[9] = static_cast<std::uint8_t>(facts.tls_major);
    (*out)[11] = static_cast<std::uint8_t>(facts.tls_minor);
    (*out)[12] = facts.full_handshake ? 1 : 0;
    (*out)[13] = facts.pin_verifier_invoked ? 1 : 0;
    (*out)[14] = facts.peer_certificate_verified ? 1 : 0;
    (*out)[15] = facts.resumed ? 1 : 0;
    (*out)[16] = facts.zero_rtt ? 1 : 0;
    std::copy(facts.alpn.begin(), facts.alpn.end(), out->begin() + 24);
    std::copy(facts.der_spki_hash.begin(), facts.der_spki_hash.end(),
              out->begin() + 56);
    return Status::Ok;
}

Status decode_quic_handshake_facts_v2(
    const std::uint8_t* bytes, std::size_t size,
    QuicHandshakeFactsV2* out) noexcept
{
    if (!bytes || !out) return Status::InvalidField;
    *out = {};
    if (size < kQuicHandshakeFactsWireSizeV2) return Status::Truncated;
    if (size > kQuicHandshakeFactsWireSizeV2) return Status::Trailing;
    if (bytes[0] != 0 || bytes[1] != 1 ||
        !std::all_of(bytes + 2, bytes + 8,
                     [](std::uint8_t value) { return value == 0; }) ||
        bytes[8] != 0 || bytes[9] != 1 || bytes[10] != 0 || bytes[11] != 3 ||
        bytes[12] > 1 || bytes[13] > 1 || bytes[14] > 1 || bytes[15] > 1 ||
        bytes[16] > 1 ||
        !std::all_of(bytes + 17, bytes + 24,
                     [](std::uint8_t value) { return value == 0; }))
        return Status::InvalidField;
    out->tls_major = 1;
    out->tls_minor = 3;
    out->full_handshake = bytes[12] == 1;
    out->pin_verifier_invoked = bytes[13] == 1;
    out->peer_certificate_verified = bytes[14] == 1;
    out->resumed = bytes[15] == 1;
    out->zero_rtt = bytes[16] == 1;
    std::copy_n(reinterpret_cast<const char*>(bytes + 24), out->alpn.size(),
                out->alpn.begin());
    std::copy_n(bytes + 56, out->der_spki_hash.size(),
                out->der_spki_hash.begin());
    return exact_text(out->alpn.data(), out->alpn.size(), kAlpn,
                      sizeof(kAlpn) - 1)
        ? Status::Ok : Status::InvalidField;
}

Status validate_quic_policy_v2(
    const fly_session_quic_connect_policy_v2* policy) noexcept
{
    if (policy == nullptr)
        return Status::InvalidField;
    if (policy->struct_size < FLY_SESSION_QUIC_CONNECT_POLICY_V2_SIZE ||
        policy->abi_version != FLY_SESSION_ABI_VERSION_2)
        return Status::BadLength;
    if (policy->reserved_zero != 0)
        return Status::NonzeroReserved;
    if (policy->require_full_tls13 != 1 || policy->forbid_resumption != 1 ||
        policy->forbid_zero_rtt != 1 ||
        !exact_text(policy->alpn, sizeof(policy->alpn), kAlpn, sizeof(kAlpn) - 1) ||
        !any_nonzero(policy->expected_der_spki_hash,
                     sizeof(policy->expected_der_spki_hash)))
        return Status::InvalidField;
    return Status::Ok;
}

Status verify_quic_handshake_v2(
    const fly_session_quic_connect_policy_v2& policy,
    const QuicHandshakeFactsV2& facts) noexcept
{
    const auto valid_policy = validate_quic_policy_v2(&policy);
    if (valid_policy != Status::Ok)
        return valid_policy;
    if (facts.tls_major != 1 || facts.tls_minor != 3 || !facts.full_handshake ||
        !facts.pin_verifier_invoked || !facts.peer_certificate_verified ||
        facts.resumed || facts.zero_rtt ||
        !exact_text(facts.alpn.data(), facts.alpn.size(), kAlpn, sizeof(kAlpn) - 1) ||
        !std::equal(facts.der_spki_hash.begin(), facts.der_spki_hash.end(),
                    policy.expected_der_spki_hash))
        return Status::InvalidField;
    return Status::Ok;
}

Status verify_quic_listener_handshake_v2(
    const QuicHandshakeFactsV2& facts) noexcept
{
    if (facts.tls_major != 1 || facts.tls_minor != 3 || !facts.full_handshake ||
        facts.pin_verifier_invoked || facts.peer_certificate_verified ||
        facts.resumed || facts.zero_rtt ||
        !exact_text(facts.alpn.data(), facts.alpn.size(), kAlpn,
                    sizeof(kAlpn) - 1) ||
        any_nonzero(facts.der_spki_hash.data(), facts.der_spki_hash.size()))
        return Status::InvalidField;
    return Status::Ok;
}

ChannelExporterRequestV1 make_channel_exporter_request_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 16>& session_id,
    const std::array<std::uint8_t, 32>& reconnect_transcript_hash)
{
    ChannelExporterRequestV1 request;
    request.label = kExporterLabel;
    request.context = concat_hash("flynes-channel-exporter-context-v1",
                                  pair_transcript_hash, session_id,
                                  reconnect_transcript_hash);
    request.output_size = 32;
    return request;
}

std::array<std::uint8_t, 16> derive_channel_id_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 16>& session_id,
    const std::array<std::uint8_t, 32>& reconnect_transcript_hash,
    const std::array<std::uint8_t, 32>& exporter)
{
    constexpr char domain[] = "flynes-channel-id-v1";
    const std::size_t domain_size = sizeof(domain) - 1;
    std::vector<std::uint8_t> bytes;
    bytes.reserve(domain_size + 32 + 16 + 32 + 32);
    bytes.insert(bytes.end(), domain, domain + domain_size);
    bytes.insert(bytes.end(), pair_transcript_hash.begin(), pair_transcript_hash.end());
    bytes.insert(bytes.end(), session_id.begin(), session_id.end());
    bytes.insert(bytes.end(), reconnect_transcript_hash.begin(),
                 reconnect_transcript_hash.end());
    bytes.insert(bytes.end(), exporter.begin(), exporter.end());
    const auto hash = sha256(bytes.data(), bytes.size());
    std::array<std::uint8_t, 16> result{};
    std::copy_n(hash.begin(), result.size(), result.begin());
    return result;
}

} // namespace flynes::session::wire
