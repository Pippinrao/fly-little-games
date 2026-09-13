#pragma once
#include <cmath>

namespace flynes::ios {
class PlaybackClock {
public:
    void reset() { started_ = false; steps_ = 0; }
    bool beginTick(double timestamp) {
        if (!std::isfinite(timestamp)) return false;
        if (!started_ || timestamp < now_ || timestamp - now_ > 0.1) {
            next_ = timestamp;
            started_ = true;
        }
        now_ = timestamp;
        steps_ = 0;
        return true;
    }
    bool frameDue() const { return started_ && steps_ < 3 && next_ <= now_ + 1e-9; }
    void didProduceSamples(unsigned samples, unsigned rate) {
        // Source duration comes from the core's fractional PCM sample clock.
        if (samples == 0 || rate == 0) { started_ = false; return; }
        next_ += static_cast<double>(samples) / rate;
        if (++steps_ == 3 && next_ <= now_) next_ = now_ + static_cast<double>(samples) / rate;
    }
private:
    bool started_ = false;
    unsigned steps_ = 0;
    double now_ = 0;
    double next_ = 0;
};
}
