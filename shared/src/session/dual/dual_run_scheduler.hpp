#ifndef FLYNES_SESSION_DUAL_DUAL_RUN_SCHEDULER_HPP
#define FLYNES_SESSION_DUAL_DUAL_RUN_SCHEDULER_HPP

/*
 * W2 / Task 6: DUAL simulation scheduler.
 *
 * The scheduler owns exactly one simulation worker (a DualRuntimePort) and the
 * input window for that engine. It is the only place that may advance the core,
 * and it enforces the three DUAL rules this release depends on:
 *
 *   1. a committed frame's input history is immutable and the state digest of a
 *      frame is verified only against that committed frame;
 *   2. a predicted frame is never a final canonical success: the verified
 *      watermark only moves to a frame both engines confirmed real, and a late
 *      trusted input at or below that watermark freezes the session instead of
 *      rewriting history;
 *   3. once paused, frozen, transport-terminal, authenticated-activity expired
 *      or shut down, step/present/input all fail closed. STREAM does not exist
 *      in this release, so the only legal outcomes are resync, reconnect, end
 *      the save or fail explicitly  -  never an automatic mode switch.
 */

#include "dual_runtime_contract.hpp"
#include "input_window.hpp"

#include <cstdint>

namespace flynes::session::dual {

/* Lifecycle vocabulary the engine projects to the user. */
enum class DualSimStateV1 : std::uint8_t
{
    Unloaded = 0,
    Ready = 1,
    Running = 2,
    Frozen = 3
};

/* Input admission vocabulary. Every rejection is explicit. */
enum class DualInputAdmitV1 : std::uint8_t
{
    Accepted = 0,
    Duplicate = 1,
    RejectedWrongOwner = 2,
    RejectedStaleRevision = 3,
    RejectedEquivocation = 4,
    RejectedWindowExceeded = 5,
    RejectedFrozen = 6
};

class DualRunSchedulerV1
{
public:
    explicit DualRunSchedulerV1(DualRuntimePort& port) noexcept;
    ~DualRunSchedulerV1() = default;

    DualRunSchedulerV1(const DualRunSchedulerV1&) = delete;
    DualRunSchedulerV1& operator=(const DualRunSchedulerV1&) = delete;

    /* Load the shared content and bind the session context. Only DUAL is legal;
     * HOST_STREAM is refused with FLY_SESSION_V2_UNAVAILABLE. */
    fly_session_result_v2 begin(
        DualModeV1 mode, const DualContentRefV1& content,
        const DualInputKeyV1& context,
        const std::array<DualOwnerKeyV1, kDualPortCountV1>& owners) noexcept;

    /* Load success is Ready, not Running. Both ends must announce before step. */
    fly_session_result_v2 announce_running() noexcept;

    /* ------------------------------------------------------------ stepping */

    /* Advance exactly one frame. On OK the produced bundle, its prediction
     * state and the digest of the resulting committed frame are all available
     * through last_plan()/last_outcome()/last_digest(). */
    fly_session_result_v2 step_next_frame() noexcept;

    /* The peer's digest for a frame this engine already stepped. Matching
     * advances state_verified_through; a mismatch freezes at that frame. A
     * frame that still carries a prediction is never marked verified, so it can
     * never be used to rewrite history later. */
    fly_session_result_v2 acknowledge_peer_digest(
        std::uint64_t frame_index, const DualStateDigestV1& peer_digest,
        bool* out_verified = nullptr) noexcept;

    /* Attempted rewrite of committed history freezes before anything is
     * applied. The window only reports the rewrite; the scheduler is the one
     * that decides to freeze. */
    fly_session_result_v2 acknowledge_committed_rewrite(
        std::uint64_t frame_index) noexcept;

    /* --------------------------------------------------------- input admit */

    /* Local samples for one frame, built canonically and offered to the window.
     * The frame is always the next frame this engine will plan. */
    fly_session_result_v2 accept_local_input(
        const DualPortInputArrayV1& samples, std::uint8_t logical_seat,
        DualInputAdmitV1* out_admit) noexcept;

    /* A trusted remote bundle either enters the window as fresh input, is a
     * duplicate, or rewrites committed history and freezes. */
    fly_session_result_v2 accept_remote_input(
        const DualInputBundleV1& bundle, DualInputAdmitV1* out_admit) noexcept;

    /* ------------------------------------------------------------ lifecycle */

    /* Timestamped authenticated activity. A monotonic clock only. */
    void observe_authenticated_activity(std::uint64_t now_ms) noexcept;

    fly_session_result_v2 pause(std::uint64_t now_ms) noexcept;
    /* Resume is only legal from a safe point both engines confirmed: the caller
     * must present peer intent and the digests observed at that safe point. */
    fly_session_result_v2 resume(std::uint64_t now_ms,
                                 const DualStateDigestV1&
                                     peer_digest_at_safe_point) noexcept;

    fly_session_result_v2 set_mode(DualModeV1 mode) noexcept;
    fly_session_result_v2 set_surface_available(bool available) noexcept;
    fly_session_result_v2 set_audio_available(bool available) noexcept;
    fly_session_result_v2 set_network_available(bool available) noexcept;
    fly_session_result_v2 mark_transport_terminal() noexcept;
    fly_session_result_v2 shutdown() noexcept;

    /* -------------------------------------------------------------- queries */

    DualSimStateV1 state() const noexcept { return state_; }
    DualFreezeReasonV1 freeze_reason() const noexcept { return freeze_reason_; }
    bool is_frozen() const noexcept
    {
        return state_ == DualSimStateV1::Frozen;
    }

    std::uint64_t current_frame() const noexcept { return current_frame_; }
    std::uint64_t next_frame() const noexcept { return next_frame_; }
    std::uint64_t state_verified_through() const noexcept
    {
        return state_verified_through_;
    }
    std::uint64_t commit_frontier() const noexcept
    {
        return window_.commit_frontier();
    }
    std::uint32_t prediction_depth() const noexcept
    {
        return prediction_depth_;
    }

    const DualInputWindowV1& window() const noexcept { return window_; }
    /* Materializing a frame is a planning action, so a mutable view is needed by
     * callers that drive the window directly (the race tests). */
    DualInputWindowV1& window() noexcept { return window_; }
    const DualWindowPlanV1& last_plan() const noexcept { return last_plan_; }
    const DualFrameOutcomeV1& last_outcome() const noexcept
    {
        return last_outcome_;
    }
    const DualStateDigestV1& last_digest() const noexcept
    {
        return last_digest_;
    }

    /* A frame is a final canonical success only when it is entirely real and
     * digest verified. A predicted frame is never one. */
    bool frame_is_canonical_success(std::uint64_t frame_index) const noexcept;

    /* Whether a step/present/input is currently permitted. */
    fly_session_result_v2 present_guard() const noexcept;

    /* True exactly when frame_index is the frame a late trusted input could
     * roll back and replay, or false (freeze) when it is committed history. */
    bool rollback_allowed(std::uint64_t frame_index,
                          std::uint32_t* out_slot) const noexcept;

private:
    fly_session_result_v2 freeze(DualFreezeReasonV1 reason) noexcept;
    fly_session_result_v2 require_running() const noexcept;
    fly_session_result_v2 handle_window_status(DualWindowStatusV1 status,
                                               DualInputAdmitV1* out_admit)
        noexcept;
    bool frame_is_verified(std::uint64_t frame_index) const noexcept;
    bool frame_is_final(std::uint64_t frame_index) const noexcept;

    DualRuntimePort& port_;
    DualInputWindowV1 window_;
    DualSimStateV1 state_ = DualSimStateV1::Unloaded;
    DualFreezeReasonV1 freeze_reason_ = DualFreezeReasonV1::None;
    DualModeV1 mode_ = DualModeV1::Dual;
    DualContentRefV1 content_{};
    DualInputKeyV1 context_{};

    /* Digests of the committed frames still inside the rollback ring. */
    std::array<bool, DualInputWindowV1::kRingSlotsV1> digest_valid_{};
    std::array<bool, DualInputWindowV1::kRingSlotsV1> digest_verified_{};
    std::array<bool, DualInputWindowV1::kRingSlotsV1> predicted_{};
    std::array<std::uint64_t, DualInputWindowV1::kRingSlotsV1> digest_frame_{};
    std::array<DualStateDigestV1, DualInputWindowV1::kRingSlotsV1> digests_{};

    DualWindowPlanV1 last_plan_{};
    DualFrameOutcomeV1 last_outcome_{};
    DualStateDigestV1 last_digest_{};

    std::uint64_t next_frame_ = 0;
    std::uint64_t current_frame_ = 0;
    std::uint64_t state_verified_through_ = 0;
    std::uint32_t prediction_depth_ = 0;
    std::uint64_t last_activity_ms_ = 0;
    bool paused_ = false;
    bool surface_available_ = true;
    bool audio_available_ = true;
    bool network_available_ = true;
    bool transport_terminal_ = false;
    bool shutdown_ = false;
};

/*
 * Rollback gate. A committed frame may only be replayed when it is above the
 * verified watermark and still inside the retained ring; returning false is a
 * hard freeze, never a silent extrapolation.
 */
bool dual_digest_rollback_allowed_v1(std::uint64_t verified_frame,
                                     std::uint64_t target_frame,
                                     std::uint32_t prediction_depth,
                                     std::uint32_t* out_slot) noexcept;

} // namespace flynes::session::dual

#endif
