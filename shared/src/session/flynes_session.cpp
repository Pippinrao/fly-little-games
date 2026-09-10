#include <flynes/flynes_session.h>
#include "session_initial_plan.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

struct fly_session_handle
{
    std::uint64_t last_tick_ns = 0u;
    bool has_tick = false;
    std::uint64_t pair_generation = 0u;
    std::uint64_t original_context_start_ns = 0u;
    flynes::session::InitialPlanLock initial_plan;
};

namespace flynes::session {
namespace {
constexpr std::uint64_t pair_timeout_ns = UINT64_C(60000000000);

bool reject(fly_session_t* session)
{
    if (session) session->initial_plan.invalidate();
    return false;
}

bool active(const fly_session_t* session)
{
    return session && session->pair_generation != 0 && !session->initial_plan.failed();
}

bool require_active(fly_session_t* session)
{
    return active(session) || reject(session);
}
} // namespace

bool start_initial_pair_attempt(fly_session_t* session, std::uint64_t generation,
                                std::uint64_t original_context_start_ns)
{
    if (!session || !session->has_tick || session->pair_generation != 0 ||
        session->initial_plan.failed() || generation == 0 ||
        original_context_start_ns > session->last_tick_ns ||
        session->last_tick_ns - original_context_start_ns >= pair_timeout_ns) return reject(session);
    session->pair_generation = generation;
    session->original_context_start_ns = original_context_start_ns;
    return true;
}

bool begin_initial_verified_pair(fly_session_t* session, const VerifiedPairEvidence& evidence)
{
    if (!require_active(session)) return false;
    if (evidence.generation != session->pair_generation) return reject(session);
    return session->initial_plan.begin(evidence);
}

bool accept_initial_plan(fly_session_t* session, const VerifiedPlanEvidence& evidence)
{
    return require_active(session) && session->initial_plan.accept_plan(evidence);
}

bool accept_initial_ack(fly_session_t* session, const VerifiedPlanEvidence& evidence)
{
    return require_active(session) && session->initial_plan.accept_ack(evidence);
}

bool accept_initial_final(fly_session_t* session, const VerifiedPlanEvidence& evidence)
{
    return require_active(session) && session->initial_plan.accept_final(evidence);
}

bool accept_initial_credentials(fly_session_t* session, const VerifiedCredentialEvidence& evidence)
{
    return require_active(session) && session->initial_plan.accept_credentials(evidence);
}

std::optional<InitialPlanCommand> poll_initial_plan_command(fly_session_t* session)
{
    return active(session) ? session->initial_plan.poll() : std::nullopt;
}

bool complete_initial_plan_command(fly_session_t* session, std::uint64_t id,
                                    std::uint64_t generation, bool success)
{
    return require_active(session) && session->initial_plan.complete(id, generation, success);
}

void invalidate_initial_pair_attempt(fly_session_t* session) noexcept
{
    if (session) session->initial_plan.invalidate();
}

std::optional<SessionInitialPlanSnapshot> initial_plan_snapshot(const fly_session_t* session)
{
    if (!session) return std::nullopt;
    const auto& plan = session->initial_plan;
    return SessionInitialPlanSnapshot{session->pair_generation != 0, active(session),
        plan.failed(), plan.not_supported(), plan.locked(), plan.mutually_locked(),
        plan.prompt_consumed(), session->pair_generation, session->original_context_start_ns,
        plan.selected_plan()};
}
} // namespace flynes::session

namespace {

fly_result validate_config(const fly_session_config* config) noexcept
{
    if (config == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (config->struct_size < FLY_SESSION_CONFIG_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (config->version != FLY_SESSION_CONFIG_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (config->reserved != 0u || config->reserved1 != 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return FLY_RESULT_OK;
}

bool valid_channel(std::uint32_t channel) noexcept
{
    return channel >= FLY_SESSION_QUIC_CONTROL && channel <= FLY_SESSION_QUIC_AUDIO;
}

void fill_genesis(fly_frame_cursor_v1* cursor) noexcept
{
    std::memset(cursor, 0, sizeof(*cursor));
    cursor->struct_size = FLY_FRAME_CURSOR_V1_SIZE;
    cursor->abi_version = FLY_FRAME_CURSOR_VERSION_1;
}

void fill_none_evidence(fly_evidence_cursor_v1* evidence) noexcept
{
    std::memset(evidence, 0, sizeof(*evidence));
    evidence->struct_size = FLY_EVIDENCE_CURSOR_V1_SIZE;
    evidence->version = FLY_EVIDENCE_CURSOR_VERSION_1;
    evidence->kind = FLY_EVIDENCE_CURSOR_NONE;
    fill_genesis(&evidence->cursor);
}

} // namespace

extern "C" fly_result fly_session_create(const fly_session_config* config,
                                         fly_session_t** session_out)
{
    if (session_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    *session_out = nullptr;
    const fly_result validation = validate_config(config);
    if (validation != FLY_RESULT_OK)
    {
        return validation;
    }
    try
    {
        auto session = std::make_unique<fly_session_handle>();
        *session_out = session.release();
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_RESULT_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" void fly_session_destroy(fly_session_t* session)
{
    delete session;
}

extern "C" fly_result fly_session_submit_event(fly_session_t* session,
                                               const fly_session_event* event)
{
    if (session == nullptr || event == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (event->struct_size < FLY_SESSION_EVENT_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (event->version != FLY_SESSION_EVENT_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (event->kind < FLY_SESSION_EVENT_USER || event->kind > FLY_SESSION_EVENT_TRANSPORT)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (event->reserved != 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_receive_stream(fly_session_t* session,
                                                 uint32_t channel,
                                                 const uint8_t* bytes,
                                                 size_t size)
{
    if (session == nullptr || (bytes == nullptr && size != 0u))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (!valid_channel(channel))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_receive_datagram(fly_session_t* session,
                                                   uint32_t channel,
                                                   const uint8_t* bytes,
                                                   size_t size)
{
    return fly_session_receive_stream(session, channel, bytes, size);
}

extern "C" fly_result fly_session_poll_command(fly_session_t* session,
                                               fly_session_command* command_out)
{
    if (session == nullptr || command_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (command_out->struct_size < FLY_SESSION_COMMAND_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (command_out->version != FLY_SESSION_COMMAND_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    const uint32_t struct_size = command_out->struct_size;
    const uint32_t version = command_out->version;
    std::memset(command_out, 0, sizeof(*command_out));
    command_out->struct_size = struct_size;
    command_out->version = version;
    command_out->kind = FLY_SESSION_COMMAND_NONE;
    return FLY_RESULT_OK;
}

extern "C" fly_result fly_session_complete_command(
    fly_session_t* session,
    const fly_session_command_result* result)
{
    if (session == nullptr || result == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (result->struct_size < FLY_SESSION_COMMAND_RESULT_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (result->version != FLY_SESSION_COMMAND_RESULT_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    return FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_tick(fly_session_t* session, uint64_t now_ns)
{
    if (session == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (session->has_tick && now_ns < session->last_tick_ns)
    {
        if (flynes::session::active(session)) session->initial_plan.invalidate();
        return FLY_RESULT_INVALID_STATE;
    }
    session->last_tick_ns = now_ns;
    session->has_tick = true;
    // Start already checked ordering; subtract only after the monotonic check.
    if (flynes::session::active(session) &&
        now_ns - session->original_context_start_ns >= flynes::session::pair_timeout_ns)
    {
        session->initial_plan.invalidate();
        return FLY_RESULT_INVALID_STATE;
    }
    return FLY_RESULT_OK;
}

extern "C" fly_result fly_session_get_snapshot(fly_session_t* session,
                                               fly_session_snapshot* snapshot_out)
{
    if (session == nullptr || snapshot_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (snapshot_out->struct_size < FLY_SESSION_SNAPSHOT_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (snapshot_out->version != FLY_SESSION_SNAPSHOT_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    const uint32_t struct_size = snapshot_out->struct_size;
    const uint32_t version = snapshot_out->version;
    std::memset(snapshot_out, 0, sizeof(*snapshot_out));
    snapshot_out->struct_size = struct_size;
    snapshot_out->version = version;
    snapshot_out->ui_state = FLY_SESSION_UI_IDLE;
    fill_genesis(&snapshot_out->committed_through);
    fill_none_evidence(&snapshot_out->state_verified_through);
    return FLY_RESULT_OK;
}
