#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace flynes::harmony {

struct MotionFrame final
{
    std::uint64_t frame_index = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgb565;
};

enum class MotionDecisionKind
{
    PRIMING,
    SOURCE,
    SYNTHESIZE,
    NATIVE_FALLBACK,
    SUSPENDED,
};

struct MotionDecision final
{
    MotionDecisionKind kind = MotionDecisionKind::SUSPENDED;
    std::uint64_t generation = 0;
    MotionFrame a;
    MotionFrame b;
};

struct MotionSchedulerStatus final
{
    bool surface_ready = false;
    bool paused = false;
    std::uint64_t generation = 0;
    std::uint64_t source_slots = 0;
    std::uint64_t synthesized_slots = 0;
    std::uint64_t hold_slots = 0;
    std::uint64_t adjacent_pairs = 0;
    std::uint64_t dropped_source_frames = 0;
};

class MotionFrameScheduler final
{
public:
    bool create_surface(std::uint64_t generation);
    bool destroy_surface(std::uint64_t generation);
    bool submit(std::uint64_t generation, std::uint64_t frame_index,
                std::uint32_t width, std::uint32_t height,
                const std::vector<std::uint8_t>& rgb565);
    MotionDecision next_slot(std::uint64_t generation, bool motion_qualified);
    void complete(const MotionDecision& decision, bool synthesized, bool presented);
    void set_paused(bool paused);
    MotionSchedulerStatus status() const;

private:
    mutable std::mutex mutex_;
    std::deque<MotionFrame> frames_;
    MotionSchedulerStatus status_;
    bool synth_phase_ = false;
};

} // namespace flynes::harmony
