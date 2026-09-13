#ifndef FLYNES_SESSION_SESSION_INVITE_CODE_HPP
#define FLYNES_SESSION_SESSION_INVITE_CODE_HPP

#include <flynes/flynes_session.h>
#include "invite_code_route.hpp"

namespace flynes::session {

// INTERNAL C++ seam for the 2026-09-13 invite-code amendment, mirroring
// session_initial_plan.hpp: one handle owns the route state; all calls,
// including C ABI tick/destroy, belong to one serialized event loop; the
// caller tick()s immediately before every callback or command dispatch.
// These functions authorize no identity release by themselves: the host
// approval and both SAS confirmations arrive through the existing anonymous
// pairing flow, and every route callback is generation/attempt fenced.

// Joiner side. submit hands the parsed six-digit code to the route and, on
// success, issues an INVITE_CODE_LOOKUP command the executor must transmit.
bool submit_invite_code(fly_session_t*, std::uint64_t attempt_id, std::string_view input,
                        std::uint64_t now_ns);
bool cancel_invite_code(fly_session_t*, std::uint64_t attempt_id);

// Adapter-reported lookup outcomes. `status` comes from a wire-validated
// INVITE_CODE_LOOKUP_RESPONSE_V1 (kind 0x0215).
bool invite_lookup_response(fly_session_t*, std::uint64_t attempt_id,
                            InviteLookupStatus status, std::uint64_t matched_generation,
                            std::uint64_t now_ns);
bool invite_joiner_host_accepted(fly_session_t*, std::uint64_t attempt_id);
bool invite_joiner_local_sas_confirmed(fly_session_t*, std::uint64_t attempt_id);
bool invite_joiner_peer_sas_confirmed(fly_session_t*, std::uint64_t attempt_id);

// Host side. publish/regenerate issue an INVITE_REGENERATE command for the
// advertisement/display switch after the route accepted the new generation.
bool invite_host_publish(fly_session_t*, std::uint64_t generation, std::string_view code_input,
                         std::uint64_t now_ns);
bool invite_host_regenerate(fly_session_t*, std::uint64_t new_generation,
                            std::string_view code_input, std::uint64_t now_ns);
bool invite_host_cancel(fly_session_t*, std::uint64_t generation);
InviteLookupStatus invite_host_on_lookup(fly_session_t*, const InviteCodeDigits& code,
                                         std::uint64_t now_ns);

// Route command plumbing for the public C ABI poll/complete path.
struct InviteRouteCommand
{
    std::uint64_t id = 0;         // route-local monotonic id, never a wire transition id
    std::uint64_t generation = 0; // attempt id (joiner) or invitation generation (host)
    fly_session_command_kind kind = FLY_SESSION_COMMAND_NONE;
};

std::optional<InviteRouteCommand> poll_invite_route_command(fly_session_t*);
bool complete_invite_route_command(fly_session_t*, std::uint64_t id, bool success);

struct SessionInviteSnapshot
{
    InviteJoinPhase join_phase = InviteJoinPhase::Idle;
    InviteHostPhase host_phase = InviteHostPhase::Idle;
    std::uint64_t join_attempt_id = 0;
    std::uint64_t host_generation = 0;
    unsigned host_attempts_left = kInviteLookupAttemptsPerInvitation;
};

std::optional<SessionInviteSnapshot> invite_route_snapshot(const fly_session_t*);

} // namespace flynes::session

#endif
