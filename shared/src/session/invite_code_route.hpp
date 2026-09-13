#ifndef FLYNES_SESSION_INVITE_CODE_ROUTE_HPP
#define FLYNES_SESSION_INVITE_CODE_ROUTE_HPP

// 2026-09-13 invite-code amendment (docs/superpowers/specs/
// 2026-09-13-nearby-invite-code-protocol-amendment.md): the six-digit manual
// code only LOCATES the host's active anonymous invitation. Authentication
// stays on the existing BLE/SAS identity flow. This route carries no identity
// data in either direction: no nickname, no long-term public key, no stable
// fingerprint, no credentials, and no game/ROM binding (a verified pairing
// ends in a game-less connected lobby).
//
// Fixed-size state, no I/O, generation-fenced, fail closed: any stale,
// malformed, out-of-order or expired input is refused and never revives a
// cancelled or superseded attempt (cases C05/C06/C07/C08/C16).
// All timestamps come from the caller's monotonic continuous clock; the 60 s
// invitation validity matches the original design's continuous clock.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace flynes::session {

inline constexpr std::size_t kInviteCodeLength = 6;
inline constexpr std::uint64_t kInviteValidityNs = UINT64_C(60000000000);
inline constexpr unsigned kInviteLookupAttemptsPerInvitation = 5;

using InviteCodeDigits = std::array<std::uint8_t, kInviteCodeLength>;

// Accepts exactly six ASCII digits (0x30..0x39) after trimming ASCII
// whitespace from both ends. Leading zeros are preserved. Anything else,
// including five or seven digits or any non-digit, returns nullopt and the
// caller must not emit a lookup request (no silent truncation).
[[nodiscard]] std::optional<InviteCodeDigits> parse_invite_code(std::string_view input);

// Status values of INVITE_CODE_LOOKUP_RESPONSE_V1 (kind 0x0215).
enum class InviteLookupStatus : std::uint8_t
{
    MatchPendingHostApproval = 1,
    NoMatch = 2,
    Expired = 3,
    RateLimited = 4,
};

// ---------------------------------------------------------------------------
// Joiner side: one live lookup attempt at a time, fenced by a caller-supplied
// monotonically increasing attempt id.
// ---------------------------------------------------------------------------
enum class InviteJoinPhase : std::uint8_t
{
    Idle = 0,
    LookingUp = 1,
    WaitingHostApproval = 2,
    WaitingSasConfirm = 3,
    Authenticated = 4,
    Cancelled = 5,
    Expired = 6,
    Ambiguous = 7,
};

class InviteCodeJoiner
{
public:
    // Starts one attempt. Refused while another attempt is live, on unparseable
    // input, or for attempt_id == 0. A started attempt must be sent under the
    // INVITE_CODE_LOOKUP command; late responses for dead attempt ids never
    // revive it.
    bool start(std::uint64_t attempt_id, std::string_view input, std::uint64_t now_ns);

    // Wire-validated response for attempt_id. Refused for any attempt id other
    // than the live one and after the attempt's deadline. Two matches carrying
    // different invitation generations fail the attempt as Ambiguous: a code
    // collision never silently picks an identity. Non-match statuses end the
    // attempt without identity reveal.
    bool on_response(std::uint64_t attempt_id, InviteLookupStatus status,
                     std::uint64_t matched_generation, std::uint64_t now_ns);

    // Host approval arrives through the existing anonymous pairing flow for
    // the live attempt only.
    bool on_host_accepted(std::uint64_t attempt_id);

    // SAS confirmations arrive through the existing BLE/SAS identity flow.
    // A single-side confirmation never connects (C07): authenticated requires
    // both. Either side may confirm first.
    bool on_local_sas_confirmed(std::uint64_t attempt_id);
    bool on_peer_sas_confirmed(std::uint64_t attempt_id);

    // Explicit user cancel of the live attempt; the attempt id is dead after
    // this and late accept/confirm callbacks are refused.
    bool cancel(std::uint64_t attempt_id);

    // Expires the live attempt at/after its 60 s deadline. Backwards time is
    // refused.
    void tick(std::uint64_t now_ns);

    [[nodiscard]] InviteJoinPhase phase() const noexcept { return phase_; }
    [[nodiscard]] std::uint64_t live_attempt_id() const noexcept { return live_attempt_id_; }
    [[nodiscard]] bool connected() const noexcept;  // game-less lobby is valid
    [[nodiscard]] bool local_sas_confirmed() const noexcept { return local_sas_confirmed_; }
    [[nodiscard]] bool peer_sas_confirmed() const noexcept { return peer_sas_confirmed_; }

private:
    bool live(std::uint64_t attempt_id) const noexcept;
    void end_attempt(InviteJoinPhase terminal) noexcept;

    InviteJoinPhase phase_ = InviteJoinPhase::Idle;
    std::uint64_t live_attempt_id_ = 0;
    // Highest attempt id ever started: attempt ids are monotonic, and a used
    // id can never be re-submitted on this route (dead attempts stay dead).
    std::uint64_t highest_attempt_id_ = 0;
    std::uint64_t deadline_ns_ = 0;
    std::uint64_t matched_generation_ = 0;
    bool local_sas_confirmed_ = false;
    bool peer_sas_confirmed_ = false;
    bool has_last_tick_ = false;
    std::uint64_t last_tick_ns_ = 0;
};

// ---------------------------------------------------------------------------
// Host side: at most one active invitation generation; regeneration and
// cancellation kill the previous generation immediately and permanently.
// ---------------------------------------------------------------------------
enum class InviteHostPhase : std::uint8_t
{
    Idle = 0,
    Active = 1,
};

class InviteCodeHost
{
public:
    // Publishes the first invitation. Refused while another invitation is
    // active or the code input is not exactly six digits.
    bool publish(std::uint64_t generation, std::string_view code_input, std::uint64_t now_ns);

    // Regeneration (N01 重新生成): kills the old generation and old QR, starts
    // the new one. Refused when idle, on bad input, or for generation == 0.
    bool regenerate(std::uint64_t new_generation, std::string_view code_input,
                    std::uint64_t now_ns);

    // Explicit 取消作废邀请; late lookups for the dead generation answer
    // NO_MATCH without state change.
    bool cancel(std::uint64_t generation);

    // Expires an active invitation at/after its 60 s deadline; backwards time
    // is refused.
    void tick(std::uint64_t now_ns);

    // Handles one wire-validated lookup request (code bytes already digit-
    // checked by the codec). Answers for the active generation only, counts
    // attempts against the per-invitation budget, and never answers for a
    // dead generation with its generation number.
    [[nodiscard]] InviteLookupStatus on_lookup(const InviteCodeDigits& code,
                                               std::uint64_t now_ns);

    [[nodiscard]] InviteHostPhase phase() const noexcept { return phase_; }
    [[nodiscard]] std::uint64_t active_generation() const noexcept
    {
        return phase_ == InviteHostPhase::Active ? generation_ : 0u;
    }
    [[nodiscard]] unsigned attempts_left() const noexcept { return attempts_left_; }

private:
    void reset() noexcept;
    bool activate(std::uint64_t generation, std::string_view code_input, std::uint64_t now_ns);

    InviteHostPhase phase_ = InviteHostPhase::Idle;
    std::uint64_t generation_ = 0;
    InviteCodeDigits code_{};
    std::uint64_t deadline_ns_ = 0;
    unsigned attempts_left_ = kInviteLookupAttemptsPerInvitation;
    bool has_last_tick_ = false;
    std::uint64_t last_tick_ns_ = 0;
};

} // namespace flynes::session

#endif
