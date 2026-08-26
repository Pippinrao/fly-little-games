#pragma once

#include <android/native_window.h>

#include <cstdint>
#include <memory>

namespace flynes::video {

struct FrameRatePlatformCall;

/** Typed surface lifecycle. Old-epoch callbacks cannot mutate the active surface. */
class PresentationCoordinator {
public:
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

private:
    enum class State { EMPTY, CREATING, READY, DESTROYING };
    State state_ = State::EMPTY;
    std::uint64_t epoch_ = 0;
    float requested_source_fps_ = 0.0f;
    std::shared_ptr<FrameRatePlatformCall> pending_platform_call_;
};

}  // namespace flynes::video
