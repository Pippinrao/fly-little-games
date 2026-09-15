#include <flynes/flynes_session.h>
#include "session_initial_plan.hpp"
#include "session_invite_code.hpp"
#include "session_receive.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <string_view>

struct fly_session_handle
{
    std::uint64_t last_tick_ns = 0u;
    bool has_tick = false;
    std::uint64_t pair_generation = 0u;
    std::uint64_t original_context_start_ns = 0u;
    flynes::session::InitialPlanLock initial_plan;

    // 2026-09-13 invite-code amendment: the route lives on the handle so the
    // public snapshot/poll/complete surface and the seam share one serialized
    // state. No identity data is stored here (design invariant).
    flynes::session::InviteCodeJoiner invite_joiner;
    flynes::session::InviteCodeHost invite_host;
    bool has_invite_command = false;
    flynes::session::InviteRouteCommand invite_command{};
    std::uint64_t invite_next_command_id = 1u;

    // Last command the public poll handed out. Mapping only: the wire
    // transition_id (16 bytes) is not representable in the public 64-bit field
    // and is never truncated into it. The generation is the one the command was
    // issued under, not the live handle generation: the seam's fence must stay
    // meaningful if a later slice ever allows an attempt restart.
    std::uint64_t polled_command_id = 0u;
    std::uint64_t polled_command_generation = 0u;
    bool has_polled_command = false;
    bool polled_command_is_invite = false;

    // Diagnostic identification of the last received chunk. Independent of the
    // trusted reducer: the receive entry points still fail closed and never
    // route a received byte to the trusted evidence seam.
    flynes::session::ReceivedFrameSummary last_received;
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

namespace {

void store_received_frame(fly_session_t* session, std::uint32_t channel,
                          const wire::AppFrame& frame) noexcept
{
    session->last_received.identified = true;
    session->last_received.channel = channel;
    session->last_received.frame_type_tag = frame.frame_type_tag;
    session->last_received.type_name = frame.type_name;
    session->last_received.object_size = frame.object_size;
    session->last_received.has_app_frame_hash = frame.has_app_frame_hash;
    if (frame.has_app_frame_hash)
    {
        std::memcpy(session->last_received.app_frame_hash, frame.app_frame_hash, 32u);
    }
}

} // namespace

bool record_received_stream(fly_session_t* session, std::uint32_t channel,
                            const std::uint8_t* bytes, std::size_t size) noexcept
{
    if (session == nullptr)
        return false;
    // Every call replaces the summary: a rejected chunk must not leave a stale
    // identification behind.
    session->last_received = ReceivedFrameSummary{};
    wire::AppFrameCursor cursor = wire::app_frame_cursor(static_cast<wire::QuicChannel>(channel),
                                                         bytes, size);
    wire::AppFrame frame{};
    bool has_frame = false;
    if (wire::next_app_frame(&cursor, &frame, &has_frame) != wire::Status::Ok || !has_frame)
    {
        return false;
    }
    store_received_frame(session, channel, frame);
    return true;
}

bool record_received_datagram(fly_session_t* session, std::uint32_t channel,
                              const std::uint8_t* bytes, std::size_t size) noexcept
{
    if (session == nullptr)
        return false;
    session->last_received = ReceivedFrameSummary{};
    wire::AppFrame frame{};
    if (wire::parse_app_frame(static_cast<wire::QuicChannel>(channel), bytes, size, &frame) !=
        wire::Status::Ok)
    {
        return false;
    }
    store_received_frame(session, channel, frame);
    return true;
}

ReceivedFrameSummary last_received_frame(const fly_session_t* session) noexcept
{
    return session == nullptr ? ReceivedFrameSummary{} : session->last_received;
}

// --- 2026-09-13 invite-code amendment: internal seam ------------------------

namespace {

bool route_command_slot_free(const fly_session_t* session)
{
    return session != nullptr && !session->has_invite_command;
}

bool issue_invite_command(fly_session_t* session, fly_session_command_kind kind,
                          std::uint64_t generation)
{
    if (session->has_invite_command)
    {
        return false;
    }
    session->invite_command.id = session->invite_next_command_id++;
    session->invite_command.generation = generation;
    session->invite_command.kind = kind;
    session->has_invite_command = true;
    return true;
}

} // namespace

bool submit_invite_code(fly_session_t* session, std::uint64_t attempt_id,
                        std::string_view input, std::uint64_t now_ns)
{
    if (!route_command_slot_free(session))
    {
        return false;
    }
    if (!session->invite_joiner.start(attempt_id, input, now_ns))
    {
        return false;
    }
    return issue_invite_command(session, FLY_SESSION_COMMAND_INVITE_CODE_LOOKUP, attempt_id);
}

bool cancel_invite_code(fly_session_t* session, std::uint64_t attempt_id)
{
    if (!route_command_slot_free(session))
    {
        return false;
    }
    if (!session->invite_joiner.cancel(attempt_id))
    {
        return false;
    }
    return issue_invite_command(session, FLY_SESSION_COMMAND_INVITE_CODE_CANCEL, attempt_id);
}

bool invite_lookup_response(fly_session_t* session, std::uint64_t attempt_id,
                            InviteLookupStatus status, std::uint64_t matched_generation,
                            std::uint64_t now_ns)
{
    return session != nullptr
        && session->invite_joiner.on_response(attempt_id, status, matched_generation, now_ns);
}

bool invite_joiner_host_accepted(fly_session_t* session, std::uint64_t attempt_id)
{
    return session != nullptr && session->invite_joiner.on_host_accepted(attempt_id);
}

bool invite_joiner_local_sas_confirmed(fly_session_t* session, std::uint64_t attempt_id)
{
    return session != nullptr && session->invite_joiner.on_local_sas_confirmed(attempt_id);
}

bool invite_joiner_peer_sas_confirmed(fly_session_t* session, std::uint64_t attempt_id)
{
    return session != nullptr && session->invite_joiner.on_peer_sas_confirmed(attempt_id);
}

bool invite_host_publish(fly_session_t* session, std::uint64_t generation,
                         std::string_view code_input, std::uint64_t now_ns)
{
    if (!route_command_slot_free(session))
    {
        return false;
    }
    if (!session->invite_host.publish(generation, code_input, now_ns))
    {
        return false;
    }
    return issue_invite_command(session, FLY_SESSION_COMMAND_INVITE_REGENERATE, generation);
}

bool invite_host_regenerate(fly_session_t* session, std::uint64_t new_generation,
                            std::string_view code_input, std::uint64_t now_ns)
{
    if (!route_command_slot_free(session))
    {
        return false;
    }
    if (!session->invite_host.regenerate(new_generation, code_input, now_ns))
    {
        return false;
    }
    return issue_invite_command(session, FLY_SESSION_COMMAND_INVITE_REGENERATE, new_generation);
}

bool invite_host_cancel(fly_session_t* session, std::uint64_t generation)
{
    return session != nullptr && session->invite_host.cancel(generation);
}

InviteLookupStatus invite_host_on_lookup(fly_session_t* session, const InviteCodeDigits& code,
                                         std::uint64_t now_ns)
{
    return session != nullptr ? session->invite_host.on_lookup(code, now_ns)
                              : InviteLookupStatus::NoMatch;
}

std::optional<InviteRouteCommand> poll_invite_route_command(fly_session_t* session)
{
    if (!session || !session->has_invite_command)
    {
        return std::nullopt;
    }
    return session->invite_command;
}

bool complete_invite_route_command(fly_session_t* session, std::uint64_t id, bool success)
{
    if (session == nullptr || !session->has_invite_command
        || session->invite_command.id != id)
    {
        return false;
    }
    const fly_session_command_kind kind = session->invite_command.kind;
    const std::uint64_t generation = session->invite_command.generation;
    session->has_invite_command = false;
    session->invite_command = InviteRouteCommand{};
    if (!success && kind == FLY_SESSION_COMMAND_INVITE_CODE_LOOKUP)
    {
        // The request never went out: the attempt is dead and no response can
        // revive it (fail closed, like every failed seam operation).
        session->invite_joiner.cancel(generation);
    }
    return true;
}

std::optional<SessionInviteSnapshot> invite_route_snapshot(const fly_session_t* session)
{
    if (session == nullptr)
    {
        return std::nullopt;
    }
    SessionInviteSnapshot snapshot;
    snapshot.join_phase = session->invite_joiner.phase();
    snapshot.host_phase = session->invite_host.phase();
    snapshot.join_attempt_id = session->invite_joiner.live_attempt_id();
    snapshot.host_generation = session->invite_host.active_generation();
    snapshot.host_attempts_left = session->invite_host.attempts_left();
    return snapshot;
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

std::optional<std::string_view> public_invite_code(const std::uint8_t* code,
                                                   std::size_t code_size) noexcept
{
    if (code == nullptr || code_size != flynes::session::kInviteCodeLength)
    {
        return std::nullopt;
    }
    const std::string_view value(reinterpret_cast<const char*>(code), code_size);
    return flynes::session::parse_invite_code(value).has_value()
               ? std::optional<std::string_view>(value)
               : std::nullopt;
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

// Raw transport bytes stay failing closed. The C2b application framing layer can
// now identify a record's type from its in-band frame type tag (app_frame.hpp)
// and validates the object with the existing codec, but that identification is
// recorded for diagnostics only: a validated-and-hashed envelope is NOT
// authentication (design spec:713), no authenticated decoder exists, and no
// received byte may reach the trusted evidence seam. Both entry points
// therefore still reject, and the diagnostic summary is never a decision input.
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
    (void)flynes::session::record_received_stream(session, channel, bytes, size);
    return FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_receive_datagram(fly_session_t* session,
                                                   uint32_t channel,
                                                   const uint8_t* bytes,
                                                   size_t size)
{
    // Same fail-closed contract as the stream entry point; the channel form does
    // not make an undecodable envelope acceptable. A datagram carries exactly one
    // record, so it is identified with datagram framing rules.
    if (session == nullptr || (bytes == nullptr && size != 0u))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (!valid_channel(channel))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    (void)flynes::session::record_received_datagram(session, channel, bytes, size);
    return FLY_RESULT_INVALID_STATE;
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
    // The v1 public command struct has no room for a 128-bit wire
    // transition_id. Only the seam's local monotonic id is exposed;
    // transition_id stays exactly zero, never truncated.
    // The invite route (2026-09-13 amendment) predates any pairing attempt;
    // its commands surface with their real v1 kind values.
    const auto invite_pending = flynes::session::poll_invite_route_command(session);
    const auto pending = invite_pending.has_value()
                             ? std::nullopt
                             : flynes::session::poll_initial_plan_command(session);
    if (invite_pending.has_value())
    {
        command_out->command_id = invite_pending->id;
        command_out->kind = static_cast<uint32_t>(invite_pending->kind);
        session->polled_command_id = invite_pending->id;
        session->polled_command_generation = invite_pending->generation;
        session->polled_command_is_invite = true;
        session->has_polled_command = true;
    }
    else if (pending.has_value())
    {
        command_out->command_id = pending->id;
        session->polled_command_id = pending->id;
        session->polled_command_generation = pending->generation;
        session->polled_command_is_invite = false;
        session->has_polled_command = true;
    }
    else
    {
        session->polled_command_id = 0u;
        session->polled_command_generation = 0u;
        session->polled_command_is_invite = false;
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
    if (session->polled_command_is_invite)
    {
        const bool accepted = flynes::session::complete_invite_route_command(
            session, result->command_id, result->result == FLY_RESULT_OK);
        return accepted ? FLY_RESULT_OK : FLY_RESULT_INVALID_STATE;
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
    // The invite route keeps its own 60 s deadlines; its tick handles
    // backwards time by expiring the live states (fail closed).
    session->invite_joiner.tick(now_ns);
    session->invite_host.tick(now_ns);
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

extern "C" fly_result fly_session_get_invite_snapshot(
    fly_session_t* session, fly_session_invite_snapshot_v1* snapshot_out)
{
    if (session == nullptr || snapshot_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (snapshot_out->struct_size < FLY_SESSION_INVITE_SNAPSHOT_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (snapshot_out->version != FLY_SESSION_INVITE_SNAPSHOT_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    const uint32_t struct_size = snapshot_out->struct_size;
    const uint32_t version = snapshot_out->version;
    std::memset(snapshot_out, 0, sizeof(*snapshot_out));
    snapshot_out->struct_size = struct_size;
    snapshot_out->version = version;
    const auto route = flynes::session::invite_route_snapshot(session);
    if (route.has_value())
    {
        snapshot_out->join_phase = static_cast<uint32_t>(route->join_phase);
        snapshot_out->host_phase = static_cast<uint32_t>(route->host_phase);
        snapshot_out->join_attempt_id = route->join_attempt_id;
        snapshot_out->host_generation = route->host_generation;
        snapshot_out->host_attempts_left = route->host_attempts_left;
    }
    return FLY_RESULT_OK;
}

extern "C" fly_result fly_session_invite_submit_code_v1(
    fly_session_t* session, uint64_t attempt_id, const uint8_t* code,
    size_t code_size, uint64_t now_ns)
{
    const auto value = public_invite_code(code, code_size);
    if (session == nullptr || attempt_id == 0u || !value.has_value())
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return flynes::session::submit_invite_code(session, attempt_id, *value, now_ns)
               ? FLY_RESULT_OK
               : FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_invite_cancel_code_v1(
    fly_session_t* session, uint64_t attempt_id)
{
    if (session == nullptr || attempt_id == 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return flynes::session::cancel_invite_code(session, attempt_id)
               ? FLY_RESULT_OK
               : FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_invite_host_publish_v1(
    fly_session_t* session, uint64_t generation, const uint8_t* code,
    size_t code_size, uint64_t now_ns)
{
    const auto value = public_invite_code(code, code_size);
    if (session == nullptr || generation == 0u || !value.has_value())
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return flynes::session::invite_host_publish(session, generation, *value, now_ns)
               ? FLY_RESULT_OK
               : FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_invite_host_regenerate_v1(
    fly_session_t* session, uint64_t generation, const uint8_t* code,
    size_t code_size, uint64_t now_ns)
{
    const auto value = public_invite_code(code, code_size);
    if (session == nullptr || generation == 0u || !value.has_value())
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return flynes::session::invite_host_regenerate(session, generation, *value, now_ns)
               ? FLY_RESULT_OK
               : FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_invite_host_cancel_v1(
    fly_session_t* session, uint64_t generation)
{
    if (session == nullptr || generation == 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return flynes::session::invite_host_cancel(session, generation)
               ? FLY_RESULT_OK
               : FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_invite_report_lookup_response_v1(
    fly_session_t* session, uint64_t attempt_id, uint32_t status,
    uint64_t matched_generation, uint64_t now_ns)
{
    if (session == nullptr || attempt_id == 0u ||
        status < FLY_SESSION_INVITE_LOOKUP_MATCH_PENDING_HOST_APPROVAL ||
        status > FLY_SESSION_INVITE_LOOKUP_RATE_LIMITED)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    const auto route_status = static_cast<flynes::session::InviteLookupStatus>(status);
    return flynes::session::invite_lookup_response(
               session, attempt_id, route_status, matched_generation, now_ns)
               ? FLY_RESULT_OK
               : FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_invite_report_host_accepted_v1(
    fly_session_t* session, uint64_t attempt_id)
{
    if (session == nullptr || attempt_id == 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return flynes::session::invite_joiner_host_accepted(session, attempt_id)
               ? FLY_RESULT_OK
               : FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_invite_confirm_local_sas_v1(
    fly_session_t* session, uint64_t attempt_id)
{
    if (session == nullptr || attempt_id == 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return flynes::session::invite_joiner_local_sas_confirmed(session, attempt_id)
               ? FLY_RESULT_OK
               : FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_invite_report_peer_sas_confirmed_v1(
    fly_session_t* session, uint64_t attempt_id)
{
    if (session == nullptr || attempt_id == 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return flynes::session::invite_joiner_peer_sas_confirmed(session, attempt_id)
               ? FLY_RESULT_OK
               : FLY_RESULT_INVALID_STATE;
}
