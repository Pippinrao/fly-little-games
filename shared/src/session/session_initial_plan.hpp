#ifndef FLYNES_SESSION_SESSION_INITIAL_PLAN_HPP
#define FLYNES_SESSION_SESSION_INITIAL_PLAN_HPP

#include <flynes/flynes_session.h>
#include "initial_plan_lock.hpp"

namespace flynes::session {

// INTERNAL C++ seam, not a deployed authentication path. Only the trusted
// adapter described in initial_plan_lock.hpp may construct verified evidence.
// One handle owns one attempt; cancellation/reconnect requires a fresh handle.
// All calls, including C ABI tick/destroy, belong to one serialized event loop.
// Caller must retain the live handle until callbacks have been drained/cancelled;
// these functions are null-safe, not safe against a dangling pointer.
// Tick the current local continuous clock immediately before EVERY callback or
// command dispatch. Before starting, tick first, then supply the original
// route-defined PairContext send/receive or QR display/scan timestamp (not the
// delayed verification arrival time).
// Before an external effect, re-poll and check the live command id/generation;
// executor deduplicates by (session instance, id), including address reuse across
// destroyed sessions. Snapshot historical flags never authorize an effect.
// Tick rejects backwards time and invalidates a live attempt; at elapsed >=60s
// it invalidates and returns INVALID_STATE. Equal ticks are allowed. Callers
// must not dispatch after tick fails. Reads before start return no command.
struct SessionInitialPlanSnapshot {
    bool started = false;
    // Started and not failed; does not mean a command is pending or that pairing
    // is incomplete. A finished initial handshake remains live until invalidated.
    bool active = false;
    bool failed = false;
    bool not_supported = false;
    bool locked = false;
    bool mutually_locked = false;
    bool prompt_consumed = false;
    std::uint64_t generation = 0;
    std::uint64_t original_context_start_ns = 0;
    wire::BearerPlanBytes selected_plan{};
};

bool start_initial_pair_attempt(fly_session_t*, std::uint64_t generation,
                                std::uint64_t original_context_start_ns);
bool begin_initial_verified_pair(fly_session_t*, const VerifiedPairEvidence&);
bool accept_initial_plan(fly_session_t*, const VerifiedPlanEvidence&);
bool accept_initial_ack(fly_session_t*, const VerifiedPlanEvidence&);
bool accept_initial_final(fly_session_t*, const VerifiedPlanEvidence&);
bool accept_initial_credentials(fly_session_t*, const VerifiedCredentialEvidence&);
std::optional<InitialPlanCommand> poll_initial_plan_command(fly_session_t*);
bool complete_initial_plan_command(fly_session_t*, std::uint64_t id,
                                    std::uint64_t generation, bool success);
void invalidate_initial_pair_attempt(fly_session_t*) noexcept;
std::optional<SessionInitialPlanSnapshot> initial_plan_snapshot(const fly_session_t*);

} // namespace flynes::session
#endif
