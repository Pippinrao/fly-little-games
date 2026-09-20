#include "link/initial_bearer_scheduler.hpp"
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
    value[0] = 4;
    value[1] = static_cast<std::uint8_t>(PairRole::Initiator);
    value[2] = static_cast<std::uint8_t>(PairRole::Responder);
    value[3] = 1;
    value[4] = 1;
    value[5] = static_cast<std::uint8_t>(PairRole::Initiator);
    value[6] = 30;
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

fly_session_port_event_v2 resource_event(
    const fly_session_op_token_v2& token, std::uint32_t kind,
    fly_session_resource_handle_v2 resource)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = token.operation_id;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 credential_event(
    const fly_session_op_token_v2& token,
    const std::array<std::uint8_t, wire::kBearerJoinParamsSizeV1>& bytes,
    fly_session_resource_handle_v2 resource, fly_session_buffer_v2_t** owned)
{
    const fly_session_bytes_v2 source{
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), 0};
    check(fly_session_buffer_create_copy_v2(source, owned) == FLY_SESSION_V2_OK,
          "fixture creates credential buffer");
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = token.operation_id;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = FLY_SESSION_PROVIDER_BEARER_CREDENTIAL_V2;
    fly_session_provider_hash_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_HASH_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    payload.buffer = *owned;
    const auto digest = wire::sha256(bytes.data(), bytes.size());
    std::copy(digest.begin(), digest.end(), payload.hash);
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

bool complete_plan_effect(InitialPlanScheduler& scheduler)
{
    const auto effect = scheduler.poll_effect();
    if (!effect) return false;
    if (effect->kind == InitialPlanEffectKind::Persist)
        return scheduler.complete(resource_event(
                   effect->token, FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2,
                   effect->token.operation_id)) == FLY_SESSION_V2_OK;
    std::vector<std::uint8_t> output;
    std::uint32_t kind = 0;
    if (effect->kind == InitialPlanEffectKind::Random)
    {
        output.assign(16, 0); output[0] = 1;
        kind = FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2;
    }
    else if (effect->kind == InitialPlanEffectKind::Hmac)
    {
        const auto digest = wire::sha256(effect->input.data(), effect->input.size());
        output.assign(digest.begin(), digest.end());
        kind = FLY_SESSION_PROVIDER_CRYPTO_MAC_V2;
    }
    else if (effect->kind == InitialPlanEffectKind::AeadSeal)
    {
        output = effect->input; output.insert(output.end(), 16, 0xa5);
        kind = FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
    }
    else if (effect->kind == InitialPlanEffectKind::AeadOpen)
    {
        output.assign(effect->input.begin(), effect->input.end() - 16);
        kind = FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
    }
    fly_session_buffer_v2_t* owned = nullptr;
    const auto event = buffer_event(effect->token, kind, output, &owned);
    const auto result = scheduler.complete(event);
    fly_session_buffer_release_v2(owned);
    return result == FLY_SESSION_V2_OK;
}

InitialPlanStartV1 plan_start(PairRole role, std::uint64_t first)
{
    InitialPlanStartV1 value{};
    value.engine_instance_id[0] = static_cast<std::uint8_t>(role);
    value.link_id[0] = 9;
    value.generation = 7;
    value.first_operation_id = first;
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

void mutually_lock(InitialPlanScheduler& initiator,
                   InitialPlanScheduler& responder)
{
    check(initiator.begin(plan_start(PairRole::Initiator, 100)) &&
              responder.begin(plan_start(PairRole::Responder, 200)),
          "plan schedulers begin");
    for (int guard = 0; guard < 80 &&
         (!initiator.mutually_locked() || !responder.mutually_locked()); ++guard)
    {
        if (initiator.poll_effect()) check(complete_plan_effect(initiator), "i plan effect");
        if (responder.poll_effect()) check(complete_plan_effect(responder), "r plan effect");
        if (const auto logical = initiator.local_logical())
        {
            check(responder.accept_peer_logical(logical->data(), logical->size()) == FLY_SESSION_V2_ACCEPTED, "r accepts plan logical");
            check(initiator.mark_local_sent() == FLY_SESSION_V2_OK, "i plan sent");
        }
        if (const auto logical = responder.local_logical())
        {
            check(initiator.accept_peer_logical(logical->data(), logical->size()) == FLY_SESSION_V2_ACCEPTED, "i accepts plan logical");
            check(responder.mark_local_sent() == FLY_SESSION_V2_OK, "r plan sent");
        }
    }
    check(initiator.mutually_locked() && responder.mutually_locked(),
          "both plans mutually lock before bearer effects");
}

InitialBearerStartV1 bearer_start(PairRole role, std::uint64_t first)
{
    InitialBearerStartV1 value{};
    value.engine_instance_id[0] = static_cast<std::uint8_t>(role);
    value.link_id[0] = 9;
    value.generation = 7;
    value.first_operation_id = first;
    value.local_role = role == PairRole::Initiator
        ? wire::PairRoleV1::Initiator : wire::PairRoleV1::Responder;
    value.transcript_hash = hash(1);
    value.session_id[0] = 8;
    value.selected_plan = selected_plan();
    value.ecdh_secret = 77;
    return value;
}

bool complete_bearer_effect(
    InitialBearerScheduler& scheduler,
    const std::array<std::uint8_t, wire::kBearerJoinParamsSizeV1>& join)
{
    const auto effect = scheduler.poll_effect();
    if (!effect) return false;
    if (effect->kind == InitialBearerEffectKind::Persist)
        return scheduler.complete(resource_event(effect->token,
                   FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2,
                   effect->token.operation_id)) == FLY_SESSION_V2_OK;
    if (effect->kind == InitialBearerEffectKind::DeriveKey ||
        effect->kind == InitialBearerEffectKind::CreateBearer ||
        effect->kind == InitialBearerEffectKind::JoinBearer)
    {
        const auto kind = effect->kind == InitialBearerEffectKind::DeriveKey
            ? FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2
            : FLY_SESSION_PROVIDER_BEARER_PATH_V2;
        const auto resource = effect->kind == InitialBearerEffectKind::DeriveKey
            ? static_cast<std::uint64_t>(300 + effect->token.operation_id)
            : static_cast<std::uint64_t>(400 + effect->token.operation_id);
        return scheduler.complete(resource_event(effect->token, kind, resource)) ==
               FLY_SESSION_V2_OK;
    }
    if (effect->kind == InitialBearerEffectKind::PrepareCredential)
    {
        fly_session_buffer_v2_t* owned = nullptr;
        const auto event = credential_event(effect->token, join,
                                             500 + effect->token.operation_id,
                                             &owned);
        const auto result = scheduler.complete(event);
        fly_session_buffer_release_v2(owned);
        return result == FLY_SESSION_V2_OK;
    }
    std::vector<std::uint8_t> output;
    if (effect->kind == InitialBearerEffectKind::AeadSeal)
    {
        output = effect->input; output.insert(output.end(), 16, 0xa5);
    }
    else if (effect->kind == InitialBearerEffectKind::AeadOpen)
        output.assign(effect->input.begin(), effect->input.end() - 16);
    else return false;
    fly_session_buffer_v2_t* owned = nullptr;
    const auto event = buffer_event(effect->token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2, output, &owned);
    const auto result = scheduler.complete(event);
    fly_session_buffer_release_v2(owned);
    return result == FLY_SESSION_V2_OK;
}

void full_dual_role_initial_bearer()
{
    InitialPlanScheduler initiator_plan;
    InitialPlanScheduler responder_plan;
    mutually_lock(initiator_plan, responder_plan);

    InitialBearerScheduler initiator(initiator_plan);
    InitialBearerScheduler responder(responder_plan);
    check(initiator.begin(bearer_start(PairRole::Initiator, 300)) &&
              responder.begin(bearer_start(PairRole::Responder, 400)),
          "bearer schedulers begin only from mutually locked plans");
    std::array<std::uint8_t, 100> credential{};
    credential[0] = 1; credential[1] = 1; credential[2] = 4; credential[3] = 8;
    std::copy_n(reinterpret_cast<const std::uint8_t*>("TEST"), 4,
                credential.begin() + 4);
    std::copy_n(reinterpret_cast<const std::uint8_t*>("password"), 8,
                credential.begin() + 36);
    std::array<std::uint8_t, wire::kBearerJoinParamsSizeV1> join{};
    check(wire::encode_bearer_join_params_v1(
              selected_plan(), 60000, credential, &join) == wire::Status::Ok,
          "fixture creates canonical join params");

    int creates = 0;
    int joins = 0;
    for (int guard = 0; guard < 80 && (!initiator.ready() || !responder.ready()); ++guard)
    {
        if (const auto effect = initiator.poll_effect())
        {
            if (effect->kind == InitialBearerEffectKind::CreateBearer) ++creates;
            if (effect->kind == InitialBearerEffectKind::JoinBearer) ++joins;
            check(complete_bearer_effect(initiator, join), "initiator bearer effect");
        }
        if (const auto effect = responder.poll_effect())
        {
            if (effect->kind == InitialBearerEffectKind::CreateBearer) ++creates;
            if (effect->kind == InitialBearerEffectKind::JoinBearer) ++joins;
            check(complete_bearer_effect(responder, join), "responder bearer effect");
        }
        if (const auto logical = initiator.local_logical())
        {
            check(responder.accept_peer_logical(logical->data(), logical->size()) ==
                      FLY_SESSION_V2_ACCEPTED,
                  "receiver accepts exact type8 logical message");
            check(initiator.mark_local_sent() == FLY_SESSION_V2_OK,
                  "creator marks type8 sent");
        }
    }
    check(initiator.ready() && responder.ready() && creates == 1 && joins == 1,
          "exactly one creator and one joiner reach bearer-ready");
    check(initiator.credential_logical_hash() ==
              responder.credential_logical_hash(),
          "both roles bind the same complete type8 logical hash");
}

} // namespace

int main()
{
    full_dual_role_initial_bearer();
    if (failures != 0) return 1;
    std::puts("initial bearer scheduler tests passed");
    return 0;
}
