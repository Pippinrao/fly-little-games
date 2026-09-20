// Exercise the production adapter, not the loopback handshake implementation.
#include "product_quic_port.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {
int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
FlynesQuicHandshakeFactsV1 valid_facts()
{
    FlynesQuicHandshakeFactsV1 facts{};
    facts.struct_size = sizeof(facts);
    facts.abi_version = FLYNES_QUIC_PROVIDER_ABI_V1;
    facts.tls_major = 1;
    facts.tls_minor = 3;
    facts.full_handshake = 1;
    constexpr char alpn[] = "flynes-nearby/2";
    facts.alpn_size = sizeof(alpn) - 1;
    std::memcpy(facts.alpn, alpn, sizeof(alpn) - 1);
    return facts;
}
bool accepted(const FlynesQuicHandshakeFactsV1& facts, std::size_t size)
{
    std::vector<std::uint8_t> raw(size);
    std::memcpy(raw.data(), &facts, std::min(size, sizeof(facts)));
    fly_session_op_token_v2 token{};
    return flynes::android::nearby::quic_deliver_handshake(nullptr, token, 1, raw);
}
}

int main()
{
    const auto valid = valid_facts();
    check(accepted(valid, sizeof(valid)), "valid provider facts accepted");
    check(!accepted(valid, sizeof(valid) - 1), "truncated provider facts rejected");
    auto bad = valid;
    bad.struct_size = 0;
    check(!accepted(bad, sizeof(bad)), "missing structure size rejected");
    bad = valid; bad.struct_size = sizeof(bad) + 1;
    check(!accepted(bad, sizeof(bad)), "declared structure beyond supplied bytes rejected");
    bad = valid; bad.abi_version = 2;
    check(!accepted(bad, sizeof(bad)), "unknown provider ABI rejected");
    bad = valid; bad.reserved_zero[1] = 1;
    check(!accepted(bad, sizeof(bad)), "reserved fields rejected");
    bad = valid; bad.full_handshake = 2;
    check(!accepted(bad, sizeof(bad)), "noncanonical full-handshake boolean rejected");
    bad = valid; bad.pin_verifier_invoked = 2;
    check(!accepted(bad, sizeof(bad)), "noncanonical pin boolean rejected");
    bad = valid; bad.peer_certificate_verified = 2;
    check(!accepted(bad, sizeof(bad)), "noncanonical verified boolean rejected");
    bad = valid; bad.resumed = 2;
    check(!accepted(bad, sizeof(bad)), "noncanonical resumed boolean rejected");
    bad = valid; bad.zero_rtt = 2;
    check(!accepted(bad, sizeof(bad)), "noncanonical zero-rtt boolean rejected");
    bad = valid; bad.tls_minor = 0;
    check(!accepted(bad, sizeof(bad)), "missing TLS fact rejected without defaults");
    bad = valid; bad.alpn[0] = 'x';
    check(!accepted(bad, sizeof(bad)), "wrong ALPN rejected");
    bad = valid; bad.alpn_size = 33;
    check(!accepted(bad, sizeof(bad)), "out-of-bounds ALPN rejected");
    auto unverified = valid;
    unverified.peer_der_spki_hash[0] = 0x7a;
    flynes::session::wire::QuicHandshakeFactsV2 decoded{};
    check(flynes::session::ports::decode_quic_provider_facts(
              reinterpret_cast<const std::uint8_t*>(&unverified), sizeof(unverified), &decoded),
          "well-formed but unverified facts remain decodable");
    check(!decoded.pin_verifier_invoked && !decoded.peer_certificate_verified &&
              decoded.der_spki_hash[0] == 0x7a,
          "decoder preserves unverified flags and actual peer hash without manufacturing trust");
    return failures == 0 ? 0 : 1;
}
