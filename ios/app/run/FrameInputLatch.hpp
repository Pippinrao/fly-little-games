#pragma once
#include <cstdint>
#include <array>
#include <algorithm>
#include <chrono>
namespace flynes::ios {
// Android's 17ms minimum applies only to normal face-button releases, never cancellation.
// Keep an unsampled short release until one frame observes it, even after a delayed tick.
class FrameInputLatch {
public:
    void update(std::uint32_t buttons) { held_ = buttons; }
    void release(std::uint32_t buttons, double downTime, double upTime) {
        const double remaining = std::max(0.0, .017-std::max(0.0,upTime-downTime));
        if (remaining <= 0) return;
        buttons &= 15u;
        pending_ |= buttons;
        for (unsigned index = 0; index < 4; ++index)
            if (buttons & (1u << index)) until_[index] = std::max(until_[index],upTime+remaining);
    }
    std::uint32_t sample(double now = monotonicTime()) {
        auto value = held_ | pending_;
        pending_ = 0;
        for (unsigned index = 0; index < 4; ++index)
            if (now < until_[index]) value |= 1u << index;
        return value;
    }
    void clear() { held_ = pending_ = 0; until_.fill(0); }
private:
    static double monotonicTime() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    std::array<double,4> until_{};
    std::uint32_t held_ = 0;
    std::uint32_t pending_ = 0;
};
}
