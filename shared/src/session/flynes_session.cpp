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

    // Last command the public poll handed out. Mapping only: the wire
    // transition_id (16 bytes) is not representable in the public 64-bit field
    // and is never truncated into it. The generation is the one the command was
    // issued under, not the live handle generation: the seam's fence must stay
    // meaningful if a later slice ever allows an attempt restart.
    std::uint64_t polled_command_id = 0u;
    std::uint64_t polled_command_generation = 0u;
    bool has_polled_command = false;
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
    // The v1 fly_session_event carries only a kind: no payload, no payload
    // length and no owner/serial token (flynes_session.h:110-116). None of the
    // private reducer entry points can be reached from it, and synthesising
    // trusted evidence from a kind is forbidden, so every kind fails closed.
    // Payload-carrying event DTOs are a versioned ABI addition, not a decision
    // this slice may take unilaterally.
    return FLY_RESULT_INVALID_STATE;
}

// Raw transport bytes stay failing closed. The frozen design identifies a wire
// object by its in-band envelope tag (family/type, design §11.3-11.4), but the
// current shared codec dispatches by an out-of-band type name supplied by the
// caller and reads no tag from the bytes (session_codec.cpp check()). Guessing a
// kind by trying candidate types, or deriving authentication from a validated
// and hashed envelope, is forbidden: a validated-and-hashed envelope is NOT
// authentication. Until a real envelope decoder exists, no received byte may
// reach the trusted evidence seam, so both entry points reject.
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
    // Same fail-closed contract as the stream entry point; the channel form does
    // not make an undecodable envelope acceptable.
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
    // The v1 public command struct has no representation for the seam's command
    // kinds and no room for a 128-bit wire transition_id. Only the seam's local
    // monotonic id is exposed; transition_id stays exactly zero, never truncated.
    const auto pending = flynes::session::poll_initial_plan_command(session);
    if (pending.has_value())
    {
        command_out->command_id = pending->id;
        session->polled_command_id = pending->id;
        session->polled_command_generation = pending->generation;
        session->has_polled_command = true;
    }
    else
    {
        session->polled_command_id = 0u;
        session->polled_command_generation = 0u;
        session->has_polled_command = false;
    }
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
    if (result->transition_id != 0u)
    {
        // A wire transition id is 16 bytes; the public field must never be used
        // as a truncated substitute for it.
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (result->command_id == 0u || !session->has_polled_command ||
        result->command_id != session->polled_command_id)
    {
        // Only the command the public poll handed out can be completed; the
        // seam remains the authority on staleness, duplicates and ordering.
        return FLY_RESULT_INVALID_STATE;
    }
    const bool accepted = flynes::session::complete_initial_plan_command(
        session, result->command_id, session->polled_command_generation,
        result->result == FLY_RESULT_OK);
    return accepted ? FLY_RESULT_OK : FLY_RESULT_INVALID_STATE;
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
    // Projected from the real reducer state. The v1 enum only defines IDLE, so a
    // live or terminal initial plan is reported as FLY_SESSION_UI_UNSPECIFIED
    // ("this ABI cannot say") rather than being misreported as idle.
    // authority_role/mode, frame cursor and evidence cursor stay unspecified
    // (zero/GENESIS/NONE): the reducer owns no seat, mode or committed-frame
    // state yet.
    const auto state = flynes::session::initial_plan_snapshot(session);
    const bool idle = state.has_value() && !state->started && !state->active &&
                      !state->failed && !state->not_supported && !state->locked &&
                      !state->mutually_locked && !state->prompt_consumed;
    snapshot_out->ui_state = idle ? FLY_SESSION_UI_IDLE : FLY_SESSION_UI_UNSPECIFIED;
    fill_genesis(&snapshot_out->committed_through);
    fill_none_evidence(&snapshot_out->state_verified_through);
    return FLY_RESULT_OK;
}
