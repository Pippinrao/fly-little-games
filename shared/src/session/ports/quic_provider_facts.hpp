#pragma once

#include "flynes_quic_provider.h"
#include "wire/quic_contract.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace flynes::session::ports {

// Parse provider-owned ABI bytes before converting to the canonical wire record.
// Never turn missing facts into expected values or normalize invalid booleans.
inline bool decode_quic_provider_facts(const std::uint8_t* bytes, std::size_t size,
                                      wire::QuicHandshakeFactsV2* out)
{
    if (out == nullptr) return false;
    *out = {};
    FlynesQuicHandshakeFactsV1 facts{};
    if (bytes == nullptr || size != sizeof(facts)) return false;
    std::memcpy(&facts, bytes, sizeof(facts));
    constexpr char alpn[] = "flynes-nearby/2";
    if (facts.struct_size != sizeof(facts) ||
        facts.abi_version != FLYNES_QUIC_PROVIDER_ABI_V1 ||
        facts.tls_major != 1 || facts.tls_minor != 3 ||
        facts.full_handshake > 1 || facts.pin_verifier_invoked > 1 ||
        facts.peer_certificate_verified > 1 || facts.resumed > 1 || facts.zero_rtt > 1 ||
        !std::all_of(std::begin(facts.reserved_zero), std::end(facts.reserved_zero),
                     [](std::uint8_t value) { return value == 0; }) ||
        facts.alpn_size != sizeof(alpn) - 1 ||
        std::memcmp(facts.alpn, alpn, sizeof(alpn) - 1) != 0)
        return false;
    out->tls_major = facts.tls_major;
    out->tls_minor = facts.tls_minor;
    out->full_handshake = facts.full_handshake == 1;
    out->pin_verifier_invoked = facts.pin_verifier_invoked == 1;
    out->peer_certificate_verified = facts.peer_certificate_verified == 1;
    out->resumed = facts.resumed == 1;
    out->zero_rtt = facts.zero_rtt == 1;
    std::copy_n(facts.alpn, facts.alpn_size, out->alpn.begin());
    std::copy_n(facts.peer_der_spki_hash, out->der_spki_hash.size(),
                out->der_spki_hash.begin());
    return true;
}

} // namespace flynes::session::ports
