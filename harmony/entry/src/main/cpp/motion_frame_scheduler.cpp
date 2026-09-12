#include "motion_frame_scheduler.hpp"

#include <utility>

namespace flynes::harmony {

bool MotionFrameScheduler::create_surface(std::uint64_t generation)
{
    if (generation == 0) return false;
    std::lock_guard lock(mutex_);
    if (status_.surface_ready && generation <= status_.generation) return false;
    status_ = {};
    status_.surface_ready = true;
    status_.generation = generation;
    frames_.clear();
    synth_phase_ = false;
    return true;
}

bool MotionFrameScheduler::destroy_surface(std::uint64_t generation)
{
    std::lock_guard lock(mutex_);
    if (!status_.surface_ready || generation != status_.generation) return false;
    status_.surface_ready = false;
    frames_.clear();
    synth_phase_ = false;
    return true;
}

bool MotionFrameScheduler::submit(std::uint64_t generation,
                                  std::uint64_t frame_index,
                                  std::uint32_t width,
                                  std::uint32_t height,
                                  const std::vector<std::uint8_t>& rgb565)
{
    if (width == 0 || height == 0 || rgb565.size() !=
        static_cast<std::size_t>(width) * height * 2U)
    {
        return false;
    }
    std::lock_guard lock(mutex_);
    if (!status_.surface_ready || generation != status_.generation) return false;
    if (!frames_.empty() && frame_index <= frames_.back().frame_index) return false;
    frames_.push_back(MotionFrame{frame_index, width, height, rgb565});
    while (frames_.size() > 3)
    {
        frames_.pop_front();
        ++status_.dropped_source_frames;
        synth_phase_ = false;
    }
    return true;
}

MotionDecision MotionFrameScheduler::next_slot(std::uint64_t generation,
                                               bool motion_qualified)
{
    std::lock_guard lock(mutex_);
    MotionDecision result;
    result.generation = generation;
    if (!status_.surface_ready || generation != status_.generation || status_.paused)
    {
        result.kind = MotionDecisionKind::SUSPENDED;
        return result;
    }
    if (!motion_qualified)
    {
        synth_phase_ = false;
        if (frames_.size() > 1) frames_.erase(frames_.begin(), frames_.end() - 1);
        result.kind = MotionDecisionKind::NATIVE_FALLBACK;
        if (!frames_.empty()) result.a = frames_.back();
        return result;
    }
    if (frames_.size() < 2)
    {
        result.kind = MotionDecisionKind::PRIMING;
        return result;
    }
    result.a = frames_[0];
    result.b = frames_[1];
    if (!synth_phase_)
    {
        result.kind = MotionDecisionKind::SOURCE;
        synth_phase_ = true;
        ++status_.source_slots;
        return result;
    }
    result.kind = MotionDecisionKind::SYNTHESIZE;
    synth_phase_ = false;
    frames_.pop_front();
    ++status_.adjacent_pairs;
    return result;
}

void MotionFrameScheduler::complete(const MotionDecision& decision,
                                    bool synthesized,
                                    bool presented)
{
    if (decision.kind != MotionDecisionKind::SYNTHESIZE) return;
    std::lock_guard lock(mutex_);
    if (decision.generation != status_.generation) return;
    if (synthesized && presented) ++status_.synthesized_slots;
    else ++status_.hold_slots;
}

void MotionFrameScheduler::set_paused(bool paused)
{
    std::lock_guard lock(mutex_);
    status_.paused = paused;
    synth_phase_ = false;
}

MotionSchedulerStatus MotionFrameScheduler::status() const
{
    std::lock_guard lock(mutex_);
    return status_;
}

} // namespace flynes::harmony
