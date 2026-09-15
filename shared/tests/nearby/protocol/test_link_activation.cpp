#include "link_activation.hpp"
#include "verified_pair_evidence.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>

using namespace flynes::session;

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

PlanHash hash(std::uint8_t value)
{
    PlanHash result{};
    result[0] = value;
    return result;
}

VerifiedPairEvidence verified_pair(PairRole local_role)
{
    VerifiedPairEvidence evidence{};
    evidence.local_role = local_role;
    evidence.generation = 7;
    evidence.transcript = hash(6);
    evidence.initiator_reveal = hash(2);
    evidence.responder_reveal = hash(3);
    evidence.initiator_capability = hash(4);
    evidence.responder_capability = hash(5);
    return VerifiedPairEvidenceTestFactory::seal(evidence);
}

LinkActivationStartV1 link_start(const VerifiedPairEvidence& pair)
{
    LinkActivationStartV1 start{};
    start.pair = pair;
    start.session_id[0] = 11;
    start.quic_listener_role = PairRole::Responder;
    start.listener_der_spki_hash[0] = 22;
    return start;
}

wire::QuicHandshakeFactsV2 handshake(const LinkActivationStartV1& start)
{
    wire::QuicHandshakeFactsV2 facts{};
    facts.tls_major = 1;
    facts.tls_minor = 3;
    facts.full_handshake = true;
    const bool connector = start.pair.local_role != start.quic_listener_role;
    facts.pin_verifier_invoked = connector;
    facts.peer_certificate_verified = connector;
    const char alpn[] = "flynes-nearby/2";
    std::copy(alpn, alpn + sizeof(alpn), facts.alpn.begin());
    if (connector)
        facts.der_spki_hash = start.listener_der_spki_hash;
    return facts;
}

wire::OwnedAppFrame early_frame()
{
    wire::OwnedAppFrame frame;
    frame.frame_type_tag = 0x0210;
    frame.type_name = "suspend_intent_v1";
    frame.object_bytes.assign(20, 7);
    return frame;
}

void drive_to_channel_bind(AuthenticatedLinkGate& gate,
                           const LinkActivationStartV1& start,
                           const std::array<std::uint8_t, 32>& exporter)
{
    check(gate.begin(start) == LinkActivationResult::Accepted,
          "authenticated pair starts link gate");
    check(gate.accept_transport(handshake(start), exporter) ==
              LinkActivationResult::Accepted,
          "full TLS, pin, and exporter authenticate transport");
    LinkChannelBindV1 bind{};
    bind.pair_transcript_hash = start.pair.transcript;
    bind.session_id = start.session_id;
    bind.reconnect_transcript_hash = start.reconnect_transcript_hash;
    bind.channel_id = wire::derive_channel_id_v1(
        start.pair.transcript, start.session_id,
        start.reconnect_transcript_hash, exporter);
    check(gate.accept_channel_bind(bind) == LinkActivationResult::Accepted,
          "exact ChannelBind opens the link-ready phase");
}

void test_two_peer_gameless_link()
{
    const auto initiator_pair = verified_pair(PairRole::Initiator);
    const auto responder_pair = verified_pair(PairRole::Responder);
    const auto initiator_start = link_start(initiator_pair);
    const auto responder_start = link_start(responder_pair);
    std::array<std::uint8_t, 32> exporter{};
    exporter[0] = 31;

    AuthenticatedLinkGate initiator(4096, 8);
    AuthenticatedLinkGate responder(4096, 8);
    drive_to_channel_bind(initiator, initiator_start, exporter);
    drive_to_channel_bind(responder, responder_start, exporter);
    check(initiator.enqueue_prebind(4, 1, early_frame()) ==
              LinkActivationResult::Accepted,
          "legal early app record is held without effects");

    check(initiator.accept_link_ready(PairRole::Initiator) ==
              LinkActivationResult::Accepted && !initiator.connected(),
          "one-sided ready never connects");
    check(initiator.accept_link_ready(PairRole::Responder) ==
              LinkActivationResult::Connected,
          "both roles ready connect without requiring a ROM");
    check(responder.accept_link_ready(PairRole::Responder) ==
              LinkActivationResult::Accepted &&
              responder.accept_link_ready(PairRole::Initiator) ==
                  LinkActivationResult::Connected,
          "responder reaches the same game-less link state");

    std::vector<wire::QueuedAppRecord> released;
    check(initiator.release_prebind(&released) == LinkActivationResult::Released &&
              released.size() == 1,
          "early app record releases only after complete link authentication");
}

void test_fail_closed_boundaries()
{
    VerifiedPairEvidence forged{};
    forged.local_role = PairRole::Initiator;
    forged.generation = 7;
    forged.transcript = hash(1);
    AuthenticatedLinkGate forged_gate(4096, 8);
    check(forged_gate.begin(link_start(forged)) ==
              LinkActivationResult::AuthFailed,
          "unsealed pair evidence cannot start transport");

    const auto pair = verified_pair(PairRole::Initiator);
    const auto start = link_start(pair);
    std::array<std::uint8_t, 32> exporter{};
    exporter[0] = 31;
    AuthenticatedLinkGate bad_pin(4096, 8);
    check(bad_pin.begin(start) == LinkActivationResult::Accepted,
          "pin fixture begins");
    check(bad_pin.enqueue_prebind(3, 1, early_frame()) ==
              LinkActivationResult::Accepted,
          "pin fixture buffers an early record");
    auto facts = handshake(start);
    facts.der_spki_hash[0] ^= 1;
    check(bad_pin.accept_transport(facts, exporter) ==
              LinkActivationResult::AuthFailed && bad_pin.failed(),
          "wrong exact DER-SPKI pin fails closed");
    std::vector<wire::QueuedAppRecord> released;
    check(bad_pin.release_prebind(&released) == LinkActivationResult::Closed &&
              released.empty(),
          "failed transport discards pre-bind bytes with zero effects");

    AuthenticatedLinkGate wrong_bind(4096, 8);
    check(wrong_bind.begin(start) == LinkActivationResult::Accepted &&
              wrong_bind.accept_transport(handshake(start), exporter) ==
                  LinkActivationResult::Accepted,
          "bind fixture authenticates transport");
    LinkChannelBindV1 bind{};
    bind.pair_transcript_hash = start.pair.transcript;
    bind.session_id = start.session_id;
    bind.channel_id[0] = 99;
    check(wrong_bind.accept_channel_bind(bind) ==
              LinkActivationResult::ProtocolViolation,
          "wrong derived channel id fails closed");

    auto listener_start = link_start(verified_pair(PairRole::Responder));
    AuthenticatedLinkGate mtls_listener(4096, 8);
    check(mtls_listener.begin(listener_start) == LinkActivationResult::Accepted,
          "listener topology fixture begins");
    auto mtls_facts = handshake(listener_start);
    mtls_facts.pin_verifier_invoked = true;
    mtls_facts.peer_certificate_verified = true;
    mtls_facts.der_spki_hash[0] = 33;
    check(mtls_listener.accept_transport(mtls_facts, exporter) ==
              LinkActivationResult::AuthFailed,
          "server-only TLS listener rejects requested client certificate evidence");
}

} // namespace

int main()
{
    test_two_peer_gameless_link();
    test_fail_closed_boundaries();
    return failures == 0 ? 0 : 1;
}
