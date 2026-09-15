// Invite-code lookup route implementation (2026-09-13 amendment). Pure
// generation-fenced state machines: fixed size, no I/O, no identity data,
// no game/ROM binding. See invite_code_route.hpp for the invariants.

#include "invite_code_route.hpp"

namespace flynes::session {
namespace {

bool ascii_space(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool ascii_digit(std::uint8_t b) noexcept
{
    return b >= 0x30u && b <= 0x39u;
}

bool join_phase_live(InviteJoinPhase phase) noexcept
{
    return phase == InviteJoinPhase::LookingUp
        || phase == InviteJoinPhase::WaitingHostApproval
        || phase == InviteJoinPhase::WaitingSasConfirm;
}

} // namespace

std::optional<InviteCodeDigits> parse_invite_code(std::string_view input)
{
    while (!input.empty() && ascii_space(input.front()))
    {
        input.remove_prefix(1);
    }
    while (!input.empty() && ascii_space(input.back()))
    {
        input.remove_suffix(1);
    }
    if (input.size() != kInviteCodeLength)
    {
        return std::nullopt;
    }
    InviteCodeDigits out{};
    for (std::size_t i = 0; i < kInviteCodeLength; ++i)
    {
        const auto byte = static_cast<std::uint8_t>(input[i]);
        if (!ascii_digit(byte))
        {
            return std::nullopt;
        }
        out[i] = byte;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Joiner
// ---------------------------------------------------------------------------

bool InviteCodeJoiner::live(std::uint64_t attempt_id) const noexcept
{
    return attempt_id != 0u && attempt_id == live_attempt_id_ && join_phase_live(phase_);
}

void InviteCodeJoiner::end_attempt(InviteJoinPhase terminal) noexcept
{
    phase_ = terminal;
    live_attempt_id_ = 0u;
    deadline_ns_ = 0u;
    matched_generation_ = 0u;
    local_sas_confirmed_ = false;
    peer_sas_confirmed_ = false;
}

bool InviteCodeJoiner::start(std::uint64_t attempt_id, std::string_view input,
                             std::uint64_t now_ns)
{
    if (attempt_id == 0u || attempt_id <= highest_attempt_id_ || join_phase_live(phase_)
        || (has_last_tick_ && now_ns < last_tick_ns_))
    {
        return false;
    }
    const auto code = parse_invite_code(input);
    if (!code.has_value())
    {
        // Refused input emits no lookup request and changes no state (C05);
        // the attempt id is not consumed either.
        return false;
    }
    (void)code;
    phase_ = InviteJoinPhase::LookingUp;
    live_attempt_id_ = attempt_id;
    highest_attempt_id_ = attempt_id;
    deadline_ns_ = now_ns + kInviteValidityNs;
    matched_generation_ = 0u;
    local_sas_confirmed_ = false;
    peer_sas_confirmed_ = false;
    has_last_tick_ = true;
    last_tick_ns_ = now_ns;
    return true;
}

bool InviteCodeJoiner::on_response(std::uint64_t attempt_id, InviteLookupStatus status,
                                   std::uint64_t matched_generation, std::uint64_t now_ns)
{
    if (!live(attempt_id) || (has_last_tick_ && now_ns < last_tick_ns_))
    {
        return false;
    }
    if (now_ns >= deadline_ns_)
    {
        end_attempt(InviteJoinPhase::Expired);
        return false;
    }
    last_tick_ns_ = now_ns;

    if (status == InviteLookupStatus::MatchPendingHostApproval)
    {
        if (phase_ == InviteJoinPhase::LookingUp)
        {
            if (matched_generation == 0u)
            {
                // Malformed match: the attempt dies without any identity move.
                end_attempt(InviteJoinPhase::Idle);
                return false;
            }
            matched_generation_ = matched_generation;
            phase_ = InviteJoinPhase::WaitingHostApproval;
            return true;
        }
        // WaitingHostApproval / WaitingSasConfirm: a same-generation repeat is
        // idempotent; a different generation means two candidates claimed the
        // same code - never silently pick one (C06/C16).
        if (matched_generation == matched_generation_)
        {
            return true;
        }
        end_attempt(InviteJoinPhase::Ambiguous);
        return false;
    }

    // NoMatch / Expired / RateLimited: the attempt ends without identity
    // reveal; the user may start a new attempt (C05/C06/C16).
    if (status == InviteLookupStatus::NoMatch || status == InviteLookupStatus::Expired
        || status == InviteLookupStatus::RateLimited)
    {
        end_attempt(InviteJoinPhase::Idle);
        return true;
    }
    return false;
}

bool InviteCodeJoiner::on_host_accepted(std::uint64_t attempt_id)
{
    if (!live(attempt_id) || phase_ != InviteJoinPhase::WaitingHostApproval)
    {
        return false;
    }
    phase_ = InviteJoinPhase::WaitingSasConfirm;
    return true;
}

bool InviteCodeJoiner::on_local_sas_confirmed(std::uint64_t attempt_id)
{
    if (!live(attempt_id) || phase_ != InviteJoinPhase::WaitingSasConfirm)
    {
        return false;
    }
    local_sas_confirmed_ = true;
    if (peer_sas_confirmed_)
    {
        phase_ = InviteJoinPhase::Authenticated;
    }
    return true;
}

bool InviteCodeJoiner::on_peer_sas_confirmed(std::uint64_t attempt_id)
{
    if (!live(attempt_id) || phase_ != InviteJoinPhase::WaitingSasConfirm)
    {
        return false;
    }
    peer_sas_confirmed_ = true;
    if (local_sas_confirmed_)
    {
        phase_ = InviteJoinPhase::Authenticated;
    }
    return true;
}

bool InviteCodeJoiner::cancel(std::uint64_t attempt_id)
{
    if (!live(attempt_id))
    {
        return false;
    }
    end_attempt(InviteJoinPhase::Cancelled);
    return true;
}

void InviteCodeJoiner::tick(std::uint64_t now_ns)
{
    if (has_last_tick_ && now_ns < last_tick_ns_)
    {
        if (join_phase_live(phase_))
        {
            end_attempt(InviteJoinPhase::Expired);
        }
        return;
    }
    has_last_tick_ = true;
    last_tick_ns_ = now_ns;
    if (join_phase_live(phase_) && now_ns >= deadline_ns_)
    {
        end_attempt(InviteJoinPhase::Expired);
    }
}

bool InviteCodeJoiner::connected() const noexcept
{
    return phase_ == InviteJoinPhase::Authenticated;
}

// ---------------------------------------------------------------------------
// Host
// ---------------------------------------------------------------------------

void InviteCodeHost::reset() noexcept
{
    phase_ = InviteHostPhase::Idle;
    generation_ = 0u;
    code_.fill(0u);
    deadline_ns_ = 0u;
    attempts_left_ = kInviteLookupAttemptsPerInvitation;
}

bool InviteCodeHost::activate(std::uint64_t generation, std::string_view code_input,
                              std::uint64_t now_ns)
{
    const auto code = parse_invite_code(code_input);
    if (!code.has_value())
    {
        return false;
    }
    phase_ = InviteHostPhase::Active;
    generation_ = generation;
    highest_generation_ = generation;
    code_ = *code;
    deadline_ns_ = now_ns + kInviteValidityNs;
    attempts_left_ = kInviteLookupAttemptsPerInvitation;
    has_last_tick_ = true;
    last_tick_ns_ = now_ns;
    return true;
}

bool InviteCodeHost::publish(std::uint64_t generation, std::string_view code_input,
                             std::uint64_t now_ns)
{
    if (generation == 0u || generation <= highest_generation_
        || phase_ != InviteHostPhase::Idle
        || (has_last_tick_ && now_ns < last_tick_ns_))
    {
        return false;
    }
    return activate(generation, code_input, now_ns);
}

bool InviteCodeHost::regenerate(std::uint64_t new_generation, std::string_view code_input,
                                std::uint64_t now_ns)
{
    if (new_generation == 0u || new_generation <= highest_generation_
        || phase_ != InviteHostPhase::Active
        || (has_last_tick_ && now_ns < last_tick_ns_))
    {
        return false;
    }
    // The new window starts fresh: the old generation, code and QR die now
    // (design N01: regenerate invalidates the old code and QR together).
    return activate(new_generation, code_input, now_ns);
}

bool InviteCodeHost::cancel(std::uint64_t generation)
{
    if (phase_ != InviteHostPhase::Active || generation != generation_)
    {
        return false;
    }
    reset();
    return true;
}

void InviteCodeHost::tick(std::uint64_t now_ns)
{
    if (has_last_tick_ && now_ns < last_tick_ns_)
    {
        if (phase_ == InviteHostPhase::Active)
        {
            reset();
        }
        return;
    }
    has_last_tick_ = true;
    last_tick_ns_ = now_ns;
    if (phase_ == InviteHostPhase::Active && now_ns >= deadline_ns_)
    {
        reset();
    }
}

InviteLookupStatus InviteCodeHost::on_lookup(const InviteCodeDigits& code, std::uint64_t now_ns)
{
    if (phase_ != InviteHostPhase::Active || (has_last_tick_ && now_ns < last_tick_ns_))
    {
        return InviteLookupStatus::NoMatch;
    }
    if (now_ns >= deadline_ns_)
    {
        return InviteLookupStatus::Expired;
    }
    if (attempts_left_ == 0u)
    {
        return InviteLookupStatus::RateLimited;
    }
    // Every processed lookup consumes budget, matches or not: the 10^6 space
    // must stay infeasible to brute force within the 60 s window.
    --attempts_left_;
    if (code != code_)
    {
        return InviteLookupStatus::NoMatch;
    }
    return InviteLookupStatus::MatchPendingHostApproval;
}

} // namespace flynes::session
