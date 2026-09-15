#include "quic_contract.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>

using flynes::session::wire::QuicHandshakeFactsV2;
using flynes::session::wire::Status;

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

fly_session_quic_connect_policy_v2 policy()
{
    fly_session_quic_connect_policy_v2 value{};
    value.struct_size = FLY_SESSION_QUIC_CONNECT_POLICY_V2_SIZE;
    value.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.require_full_tls13 = 1;
    value.forbid_resumption = 1;
    value.forbid_zero_rtt = 1;
    constexpr char alpn[] = "flynes-nearby/2";
    std::memcpy(value.alpn, alpn, sizeof(alpn));
    std::fill(std::begin(value.expected_der_spki_hash),
              std::end(value.expected_der_spki_hash), std::uint8_t{0x42});
    return value;
}

void test_policy_and_handshake()
{
    const auto expected = policy();
    check(flynes::session::wire::validate_quic_policy_v2(&expected) == Status::Ok,
          "exact full TLS 1.3 policy accepted");
    QuicHandshakeFactsV2 facts{};
    facts.tls_major = 1;
    facts.tls_minor = 3;
    facts.full_handshake = true;
    facts.pin_verifier_invoked = true;
    facts.peer_certificate_verified = true;
    constexpr char alpn[] = "flynes-nearby/2";
    std::copy(std::begin(alpn), std::end(alpn), facts.alpn.begin());
    std::fill(facts.der_spki_hash.begin(), facts.der_spki_hash.end(), std::uint8_t{0x42});
    check(flynes::session::wire::verify_quic_handshake_v2(expected, facts) == Status::Ok,
          "full handshake with exact pin accepted");
    std::array<std::uint8_t,
               flynes::session::wire::kQuicHandshakeFactsWireSizeV2> encoded{};
    QuicHandshakeFactsV2 decoded{};
    check(flynes::session::wire::encode_quic_handshake_facts_v2(
              facts, &encoded) == Status::Ok &&
              flynes::session::wire::decode_quic_handshake_facts_v2(
                  encoded.data(), encoded.size(), &decoded) == Status::Ok &&
              decoded.der_spki_hash == facts.der_spki_hash &&
              decoded.pin_verifier_invoked,
          "provider handshake facts use one strict canonical 88-byte record");

    for (int mutation = 0; mutation < 6; ++mutation)
    {
        auto bad = facts;
        if (mutation == 0) bad.full_handshake = false;
        if (mutation == 1) bad.pin_verifier_invoked = false;
        if (mutation == 2) bad.resumed = true;
        if (mutation == 3) bad.zero_rtt = true;
        if (mutation == 4) bad.alpn[14] = '1';
        if (mutation == 5) bad.der_spki_hash[0] ^= 1;
        check(flynes::session::wire::verify_quic_handshake_v2(expected, bad) != Status::Ok,
              "handshake mutation rejected");
    }
}

void test_server_only_listener_handshake()
{
    QuicHandshakeFactsV2 facts{};
    facts.tls_major = 1;
    facts.tls_minor = 3;
    facts.full_handshake = true;
    constexpr char alpn[] = "flynes-nearby/2";
    std::copy(std::begin(alpn), std::end(alpn), facts.alpn.begin());
    check(flynes::session::wire::verify_quic_listener_handshake_v2(facts) ==
              Status::Ok,
          "server-only listener accepts TLS 1.3 without a client certificate");

    auto mtls = facts;
    mtls.pin_verifier_invoked = true;
    mtls.peer_certificate_verified = true;
    mtls.der_spki_hash[0] = 1;
    check(flynes::session::wire::verify_quic_listener_handshake_v2(mtls) !=
              Status::Ok,
          "listener rejects mutual-TLS evidence outside the protocol");

    auto resumed = facts;
    resumed.resumed = true;
    check(flynes::session::wire::verify_quic_listener_handshake_v2(resumed) !=
              Status::Ok,
          "listener rejects session resumption");
}

void test_exporter_and_channel_id()
{
    std::array<std::uint8_t, 32> pair{};
    std::array<std::uint8_t, 16> session{};
    std::array<std::uint8_t, 32> reconnect{};
    std::fill(pair.begin(), pair.end(), std::uint8_t{1});
    std::fill(session.begin(), session.end(), std::uint8_t{2});
    const auto request = flynes::session::wire::make_channel_exporter_request_v1(
        pair, session, reconnect);
    check(request.label == "EXPORTER-flynes-nearby-v1" && request.output_size == 32,
          "exporter label and output length are exact");
    std::array<std::uint8_t, 32> exporter{};
    std::fill(exporter.begin(), exporter.end(), std::uint8_t{3});
    const auto channel = flynes::session::wire::derive_channel_id_v1(
        pair, session, reconnect, exporter);
    check(std::any_of(channel.begin(), channel.end(), [](std::uint8_t value) {
              return value != 0;
          }), "channel id derives from this connection exporter");
}

} // namespace

int main()
{
    test_policy_and_handshake();
    test_server_only_listener_handshake();
    test_exporter_and_channel_id();
    return failures == 0 ? 0 : 1;
}
