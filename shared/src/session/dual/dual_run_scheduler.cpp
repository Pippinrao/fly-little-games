#include "dual_run_scheduler.hpp"

#include "dual_state_digest.hpp"

#include <cstddef>
#include <new>

namespace flynes::session::dual {
namespace {

std::uint32_t digest_slot(std::uint64_t frame_index) noexcept
{
    return static_cast<std::uint32_t>(
        frame_index % DualInputWindowV1::kRingSlotsV1);
}

DualInputAdmitV1 admit_for_window_status(DualWindowStatusV1 status) noexcept
{
    switch (status) {
    case DualWindowStatusV1::Accepted:
        return DualInputAdmitV1::Accepted;
    case DualWindowStatusV1::Duplicate:
        return DualInputAdmitV1::Duplicate;
    case DualWindowStatusV1::RejectedWrongOwner:
    case DualWindowStatusV1::RejectedOtherBranch:
    case DualWindowStatusV1::RejectedOtherTimeline:
    case DualWindowStatusV1::RejectedInvalid:
        return DualInputAdmitV1::RejectedWrongOwner;
    case DualWindowStatusV1::RejectedStaleRevision:
        return DualInputAdmitV1::RejectedStaleRevision;
    case DualWindowStatusV1::RejectedCommittedHistory:
        return DualInputAdmitV1::RejectedEquivocation;
    case DualWindowStatusV1::RejectedWindowExceeded:
        return DualInputAdmitV1::RejectedWindowExceeded;
    case DualWindowStatusV1::Frozen:
        return DualInputAdmitV1::RejectedFrozen;
    case DualWindowStatusV1::Ok:
        break;
    }
    return DualInputAdmitV1::RejectedFrozen;
}

} // namespace

DualRunSchedulerV1::DualRunSchedulerV1(DualRuntimePort& port) noexcept
    : port_(port)
{
}

fly_session_result_v2 DualRunSchedulerV1::begin(
    DualModeV1 mode, const DualContentRefV1& content,
    const DualInputKeyV1& context,
    const std::array<DualOwnerKeyV1, kDualPortCountV1>& owners) noexcept
{
    /* STREAM is not implemented in this release: the mode gate is the single
     * authority and it refuses anything but DUAL. */
    const auto mode_result = evaluate_dual_mode_v1(mode);
    if (mode_result != FLY_SESSION_V2_OK) {
        mode_ = mode;
        state_ = DualSimStateV1::Unloaded;
        return mode_result;
    }
    if (!dual_input_key_is_valid_v1(context))
        return FLY_SESSION_V2_INVALID_ARGUMENT;

    try {
        const auto load_result = port_.load(content);
        if (load_result != FLY_SESSION_V2_OK)
            return load_result;
    } catch (const std::bad_alloc&) {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }

    try {
        if (window_.begin(context, owners) != DualWindowStatusV1::Ok)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
    } catch (const std::bad_alloc&) {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }

    mode_ = mode;
    content_ = content;
    context_ = context;
    digest_valid_ = {};
    predicted_ = {};
    digest_frame_ = {};
    digests_ = {};
    digest_verified_ = {};
    last_plan_ = {};
    last_outcome_ = {};
    last_digest_ = {};
    next_frame_ = 0;
    current_frame_ = 0;
    state_verified_through_ = 0;
    prediction_depth_ = 0;
    last_activity_ms_ = 0;
    paused_ = false;
    surface_available_ = true;
    audio_available_ = true;
    network_available_ = true;
    transport_terminal_ = false;
    shutdown_ = false;
    freeze_reason_ = DualFreezeReasonV1::None;
    state_ = DualSimStateV1::Running;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 DualRunSchedulerV1::freeze(
    DualFreezeReasonV1 reason) noexcept
{
    state_ = DualSimStateV1::Frozen;
    if (freeze_reason_ == DualFreezeReasonV1::None)
        freeze_reason_ = reason;
    return FLY_SESSION_V2_INVALID_STATE;
}

fly_session_result_v2 DualRunSchedulerV1::require_running() const noexcept
{
    if (shutdown_)
        return FLY_SESSION_V2_CLOSED;
    if (state_ == DualSimStateV1::Frozen)
        return FLY_SESSION_V2_INVALID_STATE;
    if (state_ != DualSimStateV1::Running)
        return FLY_SESSION_V2_INVALID_STATE;
    return FLY_SESSION_V2_OK;
}

bool DualRunSchedulerV1::frame_is_verified(std::uint64_t frame_index) const noexcept
{
    return frame_index < state_verified_through_;
}

bool DualRunSchedulerV1::frame_is_final(std::uint64_t frame_index) const noexcept
{
    /* Frames still inside the digest ring report the verdict recorded for them.
     * An older frame is final exactly when the engine advanced past it, which
     * only ever happened through a matching acknowledgement. */
    const auto slot = digest_slot(frame_index);
    if (digest_valid_[slot] && digest_frame_[slot] == frame_index)
        return digest_verified_[slot] && !predicted_[slot];
    return frame_index < state_verified_through_;
}

fly_session_result_v2 DualRunSchedulerV1::handle_window_status(
    DualWindowStatusV1 status, DualInputAdmitV1* out_admit) noexcept
{
    if (out_admit != nullptr)
        *out_admit = admit_for_window_status(status);
    switch (status) {
    case DualWindowStatusV1::Accepted:
        return FLY_SESSION_V2_ACCEPTED;
    case DualWindowStatusV1::Duplicate:
        return FLY_SESSION_V2_DUPLICATE;
    case DualWindowStatusV1::RejectedWrongOwner:
    case DualWindowStatusV1::RejectedOtherBranch:
    case DualWindowStatusV1::RejectedOtherTimeline:
        return FLY_SESSION_V2_PERMISSION_DENIED;
    case DualWindowStatusV1::RejectedStaleRevision:
        return FLY_SESSION_V2_STALE;
    case DualWindowStatusV1::RejectedInvalid:
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    case DualWindowStatusV1::RejectedCommittedHistory:
        return freeze(DualFreezeReasonV1::CommittedHistoryRewrite);
    case DualWindowStatusV1::RejectedWindowExceeded:
        return freeze(DualFreezeReasonV1::InputWindowExceeded);
    case DualWindowStatusV1::Frozen:
        return freeze(DualFreezeReasonV1::PredictionDepthExceeded);
    case DualWindowStatusV1::Ok:
        break;
    }
    return FLY_SESSION_V2_INVALID_STATE;
}

fly_session_result_v2 DualRunSchedulerV1::step_next_frame() noexcept
{
    const auto guard = require_running();
    if (guard != FLY_SESSION_V2_OK)
        return guard;

    try {
        const auto plan = window_.plan_next();
        if (plan.status == DualWindowStatusV1::Frozen) {
            prediction_depth_ = plan.prediction_depth;
            return freeze(plan.freeze);
        }
        if (plan.status != DualWindowStatusV1::Ok)
            return freeze(DualFreezeReasonV1::InputWindowExceeded);

        DualFrameOutcomeV1 outcome{};
        const auto step_result = port_.step(plan.bundle, &outcome);
        if (step_result != FLY_SESSION_V2_OK) {
            /* A failed step must never advance the core. */
            return freeze(DualFreezeReasonV1::TransportTerminal);
        }

        std::uint8_t state_bytes[1024];
        std::size_t written = 0;
        std::array<std::uint8_t, 32> state_hash{};
        const auto export_result =
            port_.export_state(state_bytes, sizeof(state_bytes), &written,
                               &state_hash);
        if (export_result != FLY_SESSION_V2_OK)
            return export_result;

        const auto digest = dual_state_digest_v1(state_bytes, written,
                                                 plan.frame_index);
        const auto slot = digest_slot(plan.frame_index);
        digests_[slot] = digest;
        digest_frame_[slot] = plan.frame_index;
        digest_valid_[slot] = true;
        digest_verified_[slot] = false;
        predicted_[slot] = plan.predicted_port_mask != 0;

        const auto mark_result =
            window_.mark_planned(plan.frame_index, plan.predicted_port_mask);
        if (mark_result != DualWindowStatusV1::Ok)
            return freeze(DualFreezeReasonV1::InputWindowExceeded);

        current_frame_ = plan.frame_index;
        next_frame_ = plan.frame_index + 1;
        prediction_depth_ = static_cast<std::uint32_t>(
            next_frame_ > window_.commit_frontier()
                ? next_frame_ - window_.commit_frontier()
                : 0);
        last_plan_ = plan;
        last_outcome_ = outcome;
        last_digest_ = digest;
        return FLY_SESSION_V2_OK;
    } catch (const std::bad_alloc&) {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
}

fly_session_result_v2 DualRunSchedulerV1::acknowledge_peer_digest(
    std::uint64_t frame_index, const DualStateDigestV1& peer_digest,
    bool* out_verified) noexcept
{
    if (out_verified != nullptr)
        *out_verified = false;
    const auto guard = require_running();
    if (guard != FLY_SESSION_V2_OK)
        return guard;

    try {
        /* A digest older than the retained ring is legitimately gone: the peer
         * is simply too late, which is a stale acknowledgement rather than a bad
         * argument. Only a frame inside the ring must actually be present. */
        if (frame_index + DualInputWindowV1::kRingSlotsV1 <=
            window_.step_frontier())
            return FLY_SESSION_V2_STALE;

        const auto slot = digest_slot(frame_index);
        if (!digest_valid_[slot] || digest_frame_[slot] != frame_index)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        if (digest_verified_[slot])
            return FLY_SESSION_V2_STALE;

        const auto gate = dual_digest_gate_v1(digests_[slot], peer_digest);
        if (gate != DualFreezeReasonV1::None)
            return freeze(gate);

        /* A frame that still carries a prediction is digest comparable but is
         * never a final canonical success, so it cannot be verified. */
        if (predicted_[slot]) {
            if (out_verified != nullptr)
                *out_verified = false;
            return FLY_SESSION_V2_ACCEPTED;
        }

        digest_verified_[slot] = true;
        if (out_verified != nullptr)
            *out_verified = true;
        if (frame_index == state_verified_through_)
            state_verified_through_ = frame_index + 1;
        return FLY_SESSION_V2_OK;
    } catch (const std::bad_alloc&) {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
}

fly_session_result_v2 DualRunSchedulerV1::acknowledge_committed_rewrite(
    std::uint64_t frame_index) noexcept
{
    if (state_ == DualSimStateV1::Frozen)
        return FLY_SESSION_V2_INVALID_STATE;
    if (frame_index >= state_verified_through_)
        return FLY_SESSION_V2_STALE;
    return freeze(DualFreezeReasonV1::CommittedHistoryRewrite);
}

fly_session_result_v2 DualRunSchedulerV1::accept_local_input(
    const DualPortInputArrayV1& samples, std::uint8_t logical_seat,
    DualInputAdmitV1* out_admit) noexcept
{
    if (out_admit != nullptr)
        *out_admit = DualInputAdmitV1::RejectedFrozen;
    const auto guard = require_running();
    if (guard != FLY_SESSION_V2_OK)
        return guard;

    try {
        return handle_window_status(
            window_.deliver_local_input(samples, logical_seat), out_admit);
    } catch (const std::bad_alloc&) {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
}

fly_session_result_v2 DualRunSchedulerV1::accept_remote_input(
    const DualInputBundleV1& bundle, DualInputAdmitV1* out_admit) noexcept
{
    if (out_admit != nullptr)
        *out_admit = DualInputAdmitV1::RejectedFrozen;
    const auto guard = require_running();
    if (guard != FLY_SESSION_V2_OK)
        return guard;

    try {
        return handle_window_status(window_.deliver_input(bundle), out_admit);
    } catch (const std::bad_alloc&) {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
}

void DualRunSchedulerV1::observe_authenticated_activity(
    std::uint64_t now_ms) noexcept
{
    if (now_ms > last_activity_ms_)
        last_activity_ms_ = now_ms;
}

fly_session_result_v2 DualRunSchedulerV1::pause(std::uint64_t now_ms) noexcept
{
    if (shutdown_)
        return FLY_SESSION_V2_CLOSED;
    if (state_ == DualSimStateV1::Frozen)
        return FLY_SESSION_V2_INVALID_STATE;
    observe_authenticated_activity(now_ms);
    paused_ = true;
    state_ = DualSimStateV1::Ready;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 DualRunSchedulerV1::resume(
    std::uint64_t now_ms,
    const DualStateDigestV1& peer_digest_at_safe_point) noexcept
{
    if (shutdown_)
        return FLY_SESSION_V2_CLOSED;
    if (state_ == DualSimStateV1::Frozen)
        return FLY_SESSION_V2_INVALID_STATE;
    if (!paused_)
        return FLY_SESSION_V2_INVALID_STATE;

    /* Resume is only legal from a safe point both engines confirmed: the
     * current committed frame must be entirely real and digest verified, and
     * the caller must present the peer digest observed at that same point. */
    const auto slot = digest_slot(current_frame_);
    if (!digest_valid_[slot] || digest_frame_[slot] != current_frame_)
        return FLY_SESSION_V2_INVALID_STATE;
    if (!digest_verified_[slot] || !window_.frame_is_real(current_frame_))
        return FLY_SESSION_V2_INVALID_STATE;
    if (dual_digest_gate_v1(digests_[slot], peer_digest_at_safe_point) !=
        DualFreezeReasonV1::None)
        return FLY_SESSION_V2_PROTOCOL_VIOLATION;

    observe_authenticated_activity(now_ms);
    paused_ = false;
    state_ = DualSimStateV1::Running;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 DualRunSchedulerV1::set_mode(DualModeV1 mode) noexcept
{
    const auto result = evaluate_dual_mode_v1(mode);
    if (result != FLY_SESSION_V2_OK)
        return result;
    mode_ = mode;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 DualRunSchedulerV1::set_surface_available(
    bool available) noexcept
{
    surface_available_ = available;
    if (!available)
        return freeze(DualFreezeReasonV1::TransportTerminal);
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 DualRunSchedulerV1::set_audio_available(
    bool available) noexcept
{
    audio_available_ = available;
    if (!available)
        return freeze(DualFreezeReasonV1::TransportTerminal);
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 DualRunSchedulerV1::set_network_available(
    bool available) noexcept
{
    network_available_ = available;
    if (!available)
        return freeze(DualFreezeReasonV1::TransportTerminal);
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 DualRunSchedulerV1::mark_transport_terminal() noexcept
{
    transport_terminal_ = true;
    return freeze(DualFreezeReasonV1::TransportTerminal);
}

fly_session_result_v2 DualRunSchedulerV1::shutdown() noexcept
{
    shutdown_ = true;
    state_ = DualSimStateV1::Frozen;
    if (freeze_reason_ == DualFreezeReasonV1::None)
        freeze_reason_ = DualFreezeReasonV1::Shutdown;
    return FLY_SESSION_V2_OK;
}

bool DualRunSchedulerV1::frame_is_canonical_success(
    std::uint64_t frame_index) const noexcept
{
    /* A frozen session has no canonical success left to report: desynchronized,
     * disconnected or shut down, the only legal outcomes are resync, reconnect,
     * end the save, or fail explicitly. */
    if (state_ == DualSimStateV1::Frozen)
        return false;
    return frame_is_final(frame_index);
}

bool DualRunSchedulerV1::rollback_allowed(std::uint64_t frame_index,
                                          std::uint32_t* out_slot) const noexcept
{
    if (out_slot == nullptr)
        return false;
    /* History strictly below the watermark is final and can never be replayed.
     * The watermark frame itself is still a legal replay target, and it is the
     * slot-zero anchor that dual_digest_rollback_allowed_v1 also uses. */
    if (frame_index < state_verified_through_)
        return false;
    const std::uint64_t anchor =
        state_verified_through_ == 0 ? 0 : state_verified_through_ - 1;
    std::uint32_t slot = 0;
    if (!dual_rollback_slot_v1(anchor, frame_index, &slot))
        return false;
    *out_slot = slot;
    return true;
}

fly_session_result_v2 DualRunSchedulerV1::present_guard() const noexcept
{
    return require_running();
}

bool dual_digest_rollback_allowed_v1(std::uint64_t verified_frame,
                                     std::uint64_t target_frame,
                                     std::uint32_t prediction_depth,
                                     std::uint32_t* out_slot) noexcept
{
    if (out_slot == nullptr)
        return false;
    /* `verified_frame` is the exclusive watermark: a target at or above it is
     * not committed history yet, and the newest replayable frame is the one just
     * below it, which is the slot-zero anchor. */
    if (target_frame >= verified_frame)
        return false;
    const std::uint64_t anchor =
        verified_frame == 0 ? 0 : verified_frame - 1;
    std::uint32_t slot = 0;
    if (!dual_rollback_slot_v1(anchor, target_frame, &slot))
        return false;
    if (prediction_depth > kDualPredictionFreezeDepthV1)
        return false;
    *out_slot = slot;
    return true;
}

} // namespace flynes::session::dual
