#include "motion_frame_scheduler.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

using flynes::harmony::MotionDecisionKind;
using flynes::harmony::MotionFrameScheduler;

static std::vector<std::uint8_t> pixels(std::uint8_t value)
{
    return std::vector<std::uint8_t>(8, value);
}

int main()
{
    MotionFrameScheduler scheduler;
    assert(scheduler.create_surface(4));
    assert(scheduler.submit(4, 10, 2, 2, pixels(10)));
    assert(scheduler.next_slot(4, true).kind == MotionDecisionKind::PRIMING);
    assert(scheduler.submit(4, 11, 2, 2, pixels(11)));

    auto a = scheduler.next_slot(4, true);
    assert(a.kind == MotionDecisionKind::SOURCE);
    assert(a.a.frame_index == 10);
    auto middle = scheduler.next_slot(4, true);
    assert(middle.kind == MotionDecisionKind::SYNTHESIZE);
    assert(middle.a.frame_index == 10 && middle.b.frame_index == 11);
    scheduler.complete(middle, false, true); // artifact guard selected HOLD.

    assert(scheduler.submit(4, 12, 2, 2, pixels(12)));
    auto b = scheduler.next_slot(4, true);
    assert(b.kind == MotionDecisionKind::SOURCE);
    assert(b.a.frame_index == 11);
    auto between = scheduler.next_slot(4, true);
    assert(between.kind == MotionDecisionKind::SYNTHESIZE);
    assert(between.a.frame_index == 11 && between.b.frame_index == 12);
    scheduler.complete(between, true, true);

    auto status = scheduler.status();
    assert(status.source_slots == 2);
    assert(status.synthesized_slots == 1);
    assert(status.hold_slots == 1);
    assert(status.adjacent_pairs == 2);

    scheduler.set_paused(true);
    assert(scheduler.next_slot(4, true).kind == MotionDecisionKind::SUSPENDED);
    scheduler.set_paused(false);
    assert(scheduler.next_slot(4, false).kind == MotionDecisionKind::NATIVE_FALLBACK);
    assert(!scheduler.submit(3, 13, 2, 2, pixels(13))); // stale generation.
    assert(scheduler.destroy_surface(4));
    assert(scheduler.next_slot(4, true).kind == MotionDecisionKind::SUSPENDED);
    return 0;
}
