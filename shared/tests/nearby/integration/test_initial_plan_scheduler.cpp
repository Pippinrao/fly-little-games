#include "link/initial_plan_scheduler.hpp"
#include "nearby/harness/verified_pair_evidence.hpp"
#include "wire/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using namespace flynes::session;

int failures = 0;
void check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

PlanHash hash(std::uint8_t value)
{
    PlanHash result{};
    result[0] = value;
    return result;
}

wire::BearerPlanBytes selected_plan()
{
    wire::BearerPlanBytes value{};
    value[0] = 2;
    value[1] = static_cast<std::uint8_t>(PairRole::Initiator);
    value[2] = 1;
    value[3] = 2;
    value[4] = 1;
    value[6] = 10;
    value[11] = 1;
    value[12] = 1;
    return value;
}

CapabilitySummary summary()
{
    CapabilitySummary value{};
    value[1] = 1;
    value[8] = 1;
    value[9] = 1;
    value[32] = 1;
    value[64] = 1;
    const auto plan = selected_plan();
    std::copy(plan.begin(), plan.end(), value.begin() + 96);
    return value;
}

VerifiedPairEvidence evidence(PairRole role)
{
    VerifiedPairEvidence value{};
    value.local_role = role;
    value.generation = 7;
    value.transcript = hash(1);
    value.initiator_reveal = hash(2);
    value.responder_reveal = hash(3);
    value.initiator_capability = hash(4);
    value.responder_capability = hash(5);
    value.initiator_summary = summary();
    value.responder_summary = summary();
    return VerifiedPairEvidenceTestFactory::seal(value);
}

fly_session_port_event_v2 buffer_event(
    const fly_session_op_token_v2& token, std::uint32_t kind,
    const std::vector<std::uint8_t>& bytes, fly_session_buffer_v2_t** owned)
{
    const fly_session_bytes_v2 source{
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), 0};
    check(fly_session_buffer_create_copy_v2(source, owned) == FLY_SESSION_V2_OK,
          "fixture creates completion buffer");
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = token.operation_id;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = *owned;
    payload.logical_size = bytes.size();
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 revision_event(
    const fly_session_op_token_v2& token)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = token.operation_id;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2;
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = token.operation_id;
    payload.value0 = token.operation_id;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

bool complete_effect(InitialPlanScheduler& scheduler)
{
    const auto effect = scheduler.poll_effect();
    if (!effect) return false;
    if (effect->kind == InitialPlanEffectKind::Persist)
        return scheduler.complete(revision_event(effect->token)) ==
               FLY_SESSION_V2_OK;
    std::vector<std::uint8_t> output;
    std::uint32_t kind = 0;
    switch (effect->kind)
    {
    case InitialPlanEffectKind::Random:
        output.assign(16, 0);
        output[0] = 1;
        kind = FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2;
        break;
    case InitialPlanEffectKind::Hmac:
    {
        const auto digest = wire::sha256(effect->input.data(), effect->input.size());
        output.assign(digest.begin(), digest.end());
        kind = FLY_SESSION_PROVIDER_CRYPTO_MAC_V2;
        break;
    }
    case InitialPlanEffectKind::AeadSeal:
        output = effect->input;
        output.insert(output.end(), 16, 0xa5);
        kind = FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
        break;
    case InitialPlanEffectKind::AeadOpen:
        if (effect->input.size() < 16) return false;
        output.assign(effect->input.begin(), effect->input.end() - 16);
        kind = FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
        break;
    case InitialPlanEffectKind::Persist:
        return false;
    }
    fly_session_buffer_v2_t* owned = nullptr;
    const auto event = buffer_event(effect->token, kind, output, &owned);
    const auto result = scheduler.complete(event);
    fly_session_buffer_release_v2(owned);
    return result == FLY_SESSION_V2_OK;
}

InitialPlanStartV1 start(PairRole role, std::uint64_t first_operation)
{
    InitialPlanStartV1 value{};
    value.engine_instance_id[0] = static_cast<std::uint8_t>(role);
    value.link_id[0] = 9;
    value.generation = 7;
    value.first_operation_id = first_operation;
    value.local_role = role == PairRole::Initiator
        ? wire::PairRoleV1::Initiator : wire::PairRoleV1::Responder;
    value.evidence = evidence(role);
    value.selected_plan = selected_plan();
    value.gatt_i2r_key = 11;
    value.gatt_r2i_key = 12;
    value.control_i2r_key = 13;
    value.control_r2i_key = 14;
    return value;
}

void full_dual_role_plan_lock()
{
    InitialPlanScheduler initiator;
    InitialPlanScheduler responder;
    check(initiator.begin(start(PairRole::Initiator, 100)) &&
              responder.begin(start(PairRole::Responder, 200)),
          "both roles begin from sealed pair evidence");

    for (int guard = 0; guard < 80 &&
         (!initiator.mutually_locked() || !responder.mutually_locked()); ++guard)
    {
        bool progressed = false;
        if (initiator.poll_effect())
        {
            check(complete_effect(initiator), "initiator effect completes");
            progressed = true;
        }
        if (responder.poll_effect())
        {
            check(complete_effect(responder), "responder effect completes");
            progressed = true;
        }
        if (const auto logical = initiator.local_logical())
        {
            check(responder.accept_peer_logical(logical->data(), logical->size()) ==
                      FLY_SESSION_V2_ACCEPTED,
                  "responder accepts exact persisted initiator message");
            check(initiator.mark_local_sent() == FLY_SESSION_V2_OK,
                  "initiator marks ordered send only after peer delivery");
            progressed = true;
        }
        if (const auto logical = responder.local_logical())
        {
            check(initiator.accept_peer_logical(logical->data(), logical->size()) ==
                      FLY_SESSION_V2_ACCEPTED,
                  "initiator accepts exact persisted responder ACK");
            check(responder.mark_local_sent() == FLY_SESSION_V2_OK,
                  "responder marks ordered send only after peer delivery");
            progressed = true;
        }
        check(progressed, "plan scheduler never deadlocks");
        if (!progressed) break;
    }
    check(initiator.mutually_locked() && responder.mutually_locked() &&
              !initiator.failed() && !responder.failed(),
          "both roles reach mutual lock only through type24/25/26 persistence");
    check(initiator.verified_plan().plan_logical_hash ==
              responder.verified_plan().plan_logical_hash &&
          initiator.verified_plan().ack_logical_hash ==
              responder.verified_plan().ack_logical_hash &&
          initiator.verified_plan().final_logical_hash ==
              responder.verified_plan().final_logical_hash,
          "both roles lock identical full logical-message hashes");
}

} // namespace

int main()
{
    full_dual_role_plan_lock();
    if (failures != 0) return 1;
    std::puts("initial plan scheduler tests passed");
    return 0;
}
