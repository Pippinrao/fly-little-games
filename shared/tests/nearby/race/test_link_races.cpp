#include "link/link_orchestrator.hpp"
#include "link/pair_pipeline.hpp"
#include "wire/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>

using namespace flynes::session;

namespace {

int failures = 0;
void check(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

PlanHash hash(std::uint8_t value)
{
    PlanHash result{}; result[0] = value; return result;
}

wire::BearerPlanBytes selected_plan()
{
    wire::BearerPlanBytes value{};
    value[0] = 2;
    value[1] = static_cast<std::uint8_t>(PairRole::Initiator);
    value[2] = 1; value[3] = 2; value[4] = 1;
    value[5] = 0; value[6] = 10; value[11] = 1; value[12] = 1;
    return value;
}

CapabilitySummary summary()
{
    CapabilitySummary value{};
    value[1] = 1; value[8] = 1; value[9] = 1; value[32] = 1;
    value[64] = 1;
    const auto selected = selected_plan();
    std::copy(selected.begin(), selected.end(), value.begin() + 96);
    return value;
}

wire::PairContributionV1 contribution(wire::PairRoleV1 role)
{
    wire::PairContributionV1 value{};
    value.role = role;
    value.bytes[12] = static_cast<std::uint8_t>(role);
    value.identity_public_key[0] = 4;
    value.identity_public_key[1] = static_cast<std::uint8_t>(role);
    const auto capability = summary();
    value.capability_summary_hash = wire::domain_hash(
        "flynes-pair-capability-summary-v1", capability.data(), capability.size());
    value.commitment = wire::pair_commitment_v1(value.bytes.data(), value.bytes.size());
    return value;
}

class Provider final : public PairPipelineCryptoProvider
{
public:
    bool verify_signature(const fly_session_op_token_v2&, PairRole role,
                          const std::array<std::uint8_t, 65>&,
                          const PlanHash&,
                          const std::array<std::uint8_t, 64>& signature) override
    {
        return signature[0] == static_cast<std::uint8_t>(role);
    }
    bool derive_sas(const PlanHash&, std::array<std::uint8_t, 6>& sas) override
    {
        sas = {{'1', '2', '3', '4', '5', '6'}};
        return true;
    }
    bool verify_key_confirmation(const fly_session_op_token_v2&, PairRole role,
                                 const PlanHash&,
                                 const PlanHash& confirmation) override
    {
        return confirmation[0] == static_cast<std::uint8_t>(role);
    }
};

VerifiedPairEvidence authenticated_pair()
{
    PairAuthStartV1 start{};
    start.generation = 7;
    start.engine_instance_id[0] = 1;
    start.link_id[0] = 2;
    start.signature_operation_ids = {{1, 2}};
    start.key_confirm_operation_ids = {{3, 4}};
    start.local_role = PairRole::Initiator;
    start.entry_mode = 1;
    start.pair_context_hash = hash(9);
    start.initiator_summary = summary();
    start.responder_summary = summary();
    start.initiator_contribution = contribution(wire::PairRoleV1::Initiator);
    start.responder_contribution = contribution(wire::PairRoleV1::Responder);
    start.initiator_commit.sender = wire::PairRoleV1::Initiator;
    start.initiator_commit.receiver = wire::PairRoleV1::Responder;
    start.initiator_commit.pair_context_hash = start.pair_context_hash;
    start.initiator_commit.commitment = start.initiator_contribution.commitment;
    start.responder_commit.sender = wire::PairRoleV1::Responder;
    start.responder_commit.receiver = wire::PairRoleV1::Initiator;
    start.responder_commit.pair_context_hash = start.pair_context_hash;
    start.responder_commit.commitment = start.responder_contribution.commitment;
    start.initiator_reveal_logical_hash = hash(2);
    start.responder_reveal_logical_hash = hash(3);
    Provider provider;
    PairPipeline pipeline(provider);
    check(pipeline.begin(start), "race fixture pair begins");
    std::array<std::uint8_t, 64> first{}; first[0] = 1; first[63] = 1;
    std::array<std::uint8_t, 64> second{}; second[0] = 2; second[63] = 1;
    check(pipeline.submit_signature(PairRole::Initiator, first) &&
              pipeline.submit_signature(PairRole::Responder, second),
          "race fixture signatures verify");
    const auto sas = pipeline.sas();
    check(sas && pipeline.approve_local(FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2,
                                        *sas),
          "race fixture SAS approves");
    check(pipeline.submit_key_confirmation(PairRole::Initiator, hash(1)) &&
              pipeline.submit_key_confirmation(PairRole::Responder, hash(2)) &&
              pipeline.accept_capability(PairRole::Initiator, summary(), hash(4)) &&
              pipeline.accept_capability(PairRole::Responder, summary(), hash(5)),
          "race fixture complete provider chain");
    const auto evidence = pipeline.take_verified_evidence();
    check(evidence && evidence->authenticated(), "race fixture seals evidence");
    return evidence ? *evidence : VerifiedPairEvidence{};
}

VerifiedPlanEvidence plan(const VerifiedPairEvidence& pair)
{
    VerifiedPlanEvidence value{};
    value.generation = pair.generation;
    value.sender = PairRole::Initiator;
    value.receiver = PairRole::Responder;
    value.transcript = pair.transcript;
    value.initiator_reveal = pair.initiator_reveal;
    value.responder_reveal = pair.responder_reveal;
    value.initiator_capability = pair.initiator_capability;
    value.responder_capability = pair.responder_capability;
    value.selected_plan = selected_plan();
    value.selected_plan_hash = wire::domain_hash(
        "flynes-selected-bearer-plan-v1", value.selected_plan.data(),
        value.selected_plan.size());
    value.plan_logical_hash = hash(6);
    return value;
}

void finish(LinkOrchestrator& link, InitialPlanCommandKind kind,
            fly_session_resource_handle_v2 resource = 0)
{
    const auto command = link.poll_plan_effect();
    check(command && command->kind == kind, "expected exact plan effect");
    if (command)
        check(link.complete_plan_effect(command->id, command->generation,
                                        FLY_SESSION_V2_OK, resource) ==
                  LinkOrchestratorResult::Accepted,
              "plan effect completes");
}

void drive_provisioned(LinkOrchestrator& link,
                       const VerifiedPairEvidence& pair)
{
    auto plan_value = plan(pair);
    check(link.accept_plan(plan_value) == LinkOrchestratorResult::Accepted,
          "plan accepted");
    finish(link, InitialPlanCommandKind::PersistPlan);
    finish(link, InitialPlanCommandKind::SendPlan);
    auto ack = plan_value;
    ack.sender = PairRole::Responder;
    ack.receiver = PairRole::Initiator;
    ack.ack_logical_hash = hash(7);
    check(link.accept_ack(ack) == LinkOrchestratorResult::Accepted, "ACK accepted");
    finish(link, InitialPlanCommandKind::PersistAckAndLock);
    auto final = plan_value;
    final.ack_logical_hash = ack.ack_logical_hash;
    final.final_logical_hash = hash(8);
    check(link.accept_final(final) == LinkOrchestratorResult::Accepted,
          "FINAL accepted");
    finish(link, InitialPlanCommandKind::PersistFinal);
    finish(link, InitialPlanCommandKind::SendFinal);
    finish(link, InitialPlanCommandKind::PersistMutualLock);
    finish(link, InitialPlanCommandKind::CreateBearer, 77);
    VerifiedCredentialEvidence credentials{final, hash(10)};
    check(link.accept_credentials(credentials) == LinkOrchestratorResult::Accepted,
          "credentials accepted after creator bearer");
    finish(link, InitialPlanCommandKind::PersistCredentials);
    finish(link, InitialPlanCommandKind::PublishCredentials);
    check(link.state() == LinkOrchestratorState::BearerReady,
          "durable plan and credentials produce bearer-ready only");
}

LinkActivationStartV1 activation(const VerifiedPairEvidence& pair)
{
    LinkActivationStartV1 value{};
    value.pair = pair;
    value.session_id[0] = 11;
    value.reconnect_transcript_hash = hash(12);
    value.quic_listener_role = PairRole::Responder;
    value.listener_der_spki_hash = hash(13);
    return value;
}

wire::QuicHandshakeFactsV2 valid_quic_facts()
{
    wire::QuicHandshakeFactsV2 facts{};
    facts.tls_major = 1;
    facts.tls_minor = 3;
    const char alpn[] = "flynes-nearby/2";
    std::copy(alpn, alpn + sizeof(alpn), facts.alpn.begin());
    facts.der_spki_hash = hash(13);
    facts.full_handshake = true;
    facts.pin_verifier_invoked = true;
    facts.peer_certificate_verified = true;
    return facts;
}

LinkChannelBindV1 valid_bind(const VerifiedPairEvidence& pair)
{
    LinkChannelBindV1 bind{};
    bind.pair_transcript_hash = pair.transcript;
    bind.session_id[0] = 11;
    bind.reconnect_transcript_hash = hash(12);
    bind.channel_id = wire::derive_channel_id_v1(
        pair.transcript, bind.session_id, bind.reconnect_transcript_hash,
        hash(14));
    return bind;
}

void drive_binding(LinkOrchestrator& link, const VerifiedPairEvidence& pair,
                   fly_session_resource_handle_v2 connection = 99)
{
    drive_provisioned(link, pair);
    check(link.request_quic(activation(pair)) == LinkOrchestratorResult::Accepted,
          "binding fixture starts QUIC");
    const auto effect = link.poll_quic_effect();
    check(effect && link.complete_quic(
              effect->id, effect->generation, FLY_SESSION_V2_OK, connection,
              valid_quic_facts(), hash(14)) == LinkOrchestratorResult::Accepted,
          "binding fixture validates pinned QUIC");
}

void test_cancellation_boundaries_release_exact_resources()
{
    const auto pair = authenticated_pair();

    LinkOrchestrator stale_cancel;
    check(stale_cancel.begin(pair) == LinkOrchestratorResult::Accepted &&
              stale_cancel.cancel(pair.generation + 1) ==
                  LinkOrchestratorResult::Stale &&
              stale_cancel.state() == LinkOrchestratorState::PlanNegotiating &&
              !stale_cancel.poll_plan_effect().has_value(),
          "stale generation cancel cannot mutate current plan work");

    LinkOrchestrator plan_cancel;
    check(plan_cancel.begin(pair) == LinkOrchestratorResult::Accepted,
          "plan cancellation fixture begins");
    check(plan_cancel.accept_plan(plan(pair)) ==
              LinkOrchestratorResult::Accepted,
          "plan cancellation fixture has a durable effect in flight");
    const auto persist = plan_cancel.poll_plan_effect();
    check(persist && plan_cancel.cancel(pair.generation) ==
              LinkOrchestratorResult::Accepted &&
              plan_cancel.complete_plan_effect(
                  persist->id, persist->generation, FLY_SESSION_V2_OK, 71) ==
                  LinkOrchestratorResult::Stale &&
              plan_cancel.take_cleanup_resource() == 71 &&
              plan_cancel.take_cleanup_resource() == 0,
          "late plan resource after cancel is released exactly once");

    LinkOrchestrator bearer_cancel;
    check(bearer_cancel.begin(pair) == LinkOrchestratorResult::Accepted,
          "bearer cancellation fixture begins");
    drive_provisioned(bearer_cancel, pair);
    check(bearer_cancel.cancel(pair.generation) ==
              LinkOrchestratorResult::Accepted &&
              bearer_cancel.take_cleanup_resource() == 77 &&
              bearer_cancel.take_cleanup_resource() == 0,
          "cancellation after bearer readiness releases the bearer once");

    LinkOrchestrator quic_cancel;
    check(quic_cancel.begin(pair) == LinkOrchestratorResult::Accepted,
          "QUIC cancellation fixture begins");
    drive_provisioned(quic_cancel, pair);
    check(quic_cancel.request_quic(activation(pair)) ==
              LinkOrchestratorResult::Accepted,
          "QUIC cancellation fixture has an in-flight start");
    const auto pending_quic = quic_cancel.poll_quic_effect();
    check(pending_quic && quic_cancel.cancel(pair.generation) ==
              LinkOrchestratorResult::Accepted &&
              quic_cancel.take_cleanup_resource() == 77 &&
              quic_cancel.complete_quic(
                  pending_quic->id, pending_quic->generation,
                  FLY_SESSION_V2_OK, 88, valid_quic_facts(), hash(14)) ==
                  LinkOrchestratorResult::Stale &&
              quic_cancel.take_cleanup_resource() == 88 &&
              quic_cancel.take_cleanup_resource() == 0,
          "QUIC cancel releases bearer and late connection exactly once");

    LinkOrchestrator bind_cancel;
    check(bind_cancel.begin(pair) == LinkOrchestratorResult::Accepted,
          "binding cancellation fixture begins");
    drive_binding(bind_cancel, pair, 99);
    check(bind_cancel.cancel(pair.generation) ==
              LinkOrchestratorResult::Accepted &&
              bind_cancel.take_cleanup_resource() == 99 &&
              bind_cancel.take_cleanup_resource() == 77 &&
              bind_cancel.take_cleanup_resource() == 0,
          "binding cancel releases connection before bearer exactly once");

    LinkOrchestrator connected_cancel;
    check(connected_cancel.begin(pair) == LinkOrchestratorResult::Accepted,
          "connected cancellation fixture begins");
    drive_binding(connected_cancel, pair, 109);
    const auto bind = valid_bind(pair);
    check(connected_cancel.accept_channel_bind(bind) ==
              LinkOrchestratorResult::Accepted &&
              connected_cancel.accept_link_ready(PairRole::Initiator) ==
                  LinkOrchestratorResult::Accepted &&
              connected_cancel.accept_link_ready(PairRole::Responder) ==
                  LinkOrchestratorResult::Connected &&
              connected_cancel.cancel(pair.generation) ==
                  LinkOrchestratorResult::Accepted &&
              connected_cancel.take_cleanup_resource() == 109 &&
              connected_cancel.take_cleanup_resource() == 77 &&
              connected_cancel.take_cleanup_resource() == 0,
          "connected-lobby cancel releases connection and bearer once");
}

void test_gates_and_stale_cleanup()
{
    LinkOrchestrator unauthenticated;
    check(unauthenticated.begin(VerifiedPairEvidence{}) ==
              LinkOrchestratorResult::AuthFailed &&
              !unauthenticated.poll_plan_effect() &&
              unauthenticated.request_quic(activation(VerifiedPairEvidence{})) ==
                  LinkOrchestratorResult::InvalidState,
          "unverified pair cannot emit bearer or QUIC work");

    const auto pair = authenticated_pair();
    LinkOrchestrator cancelled;
    check(cancelled.begin(pair) == LinkOrchestratorResult::Accepted,
          "cancel fixture begins");
    check(cancelled.cancel(pair.generation) == LinkOrchestratorResult::Accepted,
          "current generation cancels");
    check(cancelled.complete_quic(1, pair.generation, FLY_SESSION_V2_OK, 88,
                                  {}, {}) == LinkOrchestratorResult::Stale &&
              cancelled.take_cleanup_resource() == 88 &&
              cancelled.state() == LinkOrchestratorState::Cancelled,
          "late QUIC success only releases its stale resource");

    LinkOrchestrator link;
    check(link.begin(pair) == LinkOrchestratorResult::Accepted,
          "authenticated link begins");
    check(link.request_quic(activation(pair)) ==
              LinkOrchestratorResult::InvalidState,
          "QUIC is forbidden before mutual plan and bearer completion");
    drive_provisioned(link, pair);
    check(link.request_quic(activation(pair)) == LinkOrchestratorResult::Accepted,
          "QUIC starts only after complete provisioning");
    const auto quic = link.poll_quic_effect();
    check(quic && quic->generation == pair.generation,
          "QUIC effect carries the exact generation");
    if (!quic) return;
    const auto facts = valid_quic_facts();
    check(link.complete_quic(quic->id, quic->generation, FLY_SESSION_V2_OK,
                             99, facts, hash(14)) ==
              LinkOrchestratorResult::Accepted,
          "pinned TLS13 QUIC completion enters binding");
    const auto bind = valid_bind(pair);
    check(link.accept_channel_bind(bind) == LinkOrchestratorResult::Accepted &&
              link.accept_link_ready(PairRole::Initiator) ==
                  LinkOrchestratorResult::Accepted &&
              link.accept_link_ready(PairRole::Responder) ==
                  LinkOrchestratorResult::Connected &&
              link.state() == LinkOrchestratorState::ConnectedLobby,
          "ChannelBind plus both LINK_READY reaches game-less lobby");
}

} // namespace

int main()
{
    test_gates_and_stale_cleanup();
    test_cancellation_boundaries_release_exact_resources();
    return failures == 0 ? 0 : 1;
}
