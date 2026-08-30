#pragma once

#include <android/native_window.h>

#include <cstdint>
#include <memory>

namespace flynes::video {

struct FrameRatePlatformCall;

/** Typed surface lifecycle. Old-epoch callbacks cannot mutate the active surface. */
class PresentationCoordinator {
public:
    enum class TemporalState : int {
        IMMEDIATE_NATIVE = 0,
        PRIMING = 1,
        MOTION_COMPENSATING = 2,
        BUFFERED_NATIVE_HOLD = 3,
        PRIMING_SHADOW = 4,
        DRAINING = 5,
        SURFACE_SUSPENDED_HOLD = 6
    };
    enum class FrameRateVoteResult : int {
        CLEARED = 0,
        APPLIED = 1,
        UNSUPPORTED = 2,
        FAILED = 3,
        STALE_EPOCH = 4
    };

    bool begin_create(std::uint64_t epoch);
    void complete_create(std::uint64_t epoch, bool ready);
    bool begin_destroy(std::uint64_t epoch);
    void complete_destroy(std::uint64_t epoch);
    bool is_active(std::uint64_t epoch) const;
    bool owns_epoch(std::uint64_t epoch) const;
    FrameRateVoteResult request_frame_rate(std::uint64_t epoch,
                                           ANativeWindow* window, float source_fps);
    FrameRateVoteResult clear_frame_rate(std::uint64_t epoch,
                                         ANativeWindow* window);
    std::uint64_t epoch() const { return epoch_; }
    float requested_source_fps() const { return requested_source_fps_; }
    bool begin_motion_priming(std::uint64_t epoch);
    bool begin_motion_shadow(std::uint64_t epoch);
    bool activate_motion(std::uint64_t epoch);
    bool enter_buffered_hold(std::uint64_t epoch);
    bool enter_draining(std::uint64_t epoch);
    bool resume_immediate_native(std::uint64_t epoch);
    bool suspend_surface(std::uint64_t epoch);
    TemporalState temporal_state() const { return temporal_state_; }
    bool swappy_owns_pacing() const {
        return temporal_state_ == TemporalState::PRIMING
                || temporal_state_ == TemporalState::MOTION_COMPENSATING;
    }

private:
    enum class State { EMPTY, CREATING, READY, DESTROYING };
    State state_ = State::EMPTY;
    std::uint64_t epoch_ = 0;
    float requested_source_fps_ = 0.0f;
    TemporalState temporal_state_ = TemporalState::IMMEDIATE_NATIVE;
    std::shared_ptr<FrameRatePlatformCall> pending_platform_call_;
};

}  // namespace flynes::video
