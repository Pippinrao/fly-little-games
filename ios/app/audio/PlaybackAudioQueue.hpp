#pragma once
#include <cstdint>
namespace flynes::ios {
class PlaybackAudioQueue {
public:
    bool reserve() { if (count_ >= 4) return false; ++count_; return true; }
    void finish(std::uint64_t generation) { if (generation == generation_ && count_ > 0) --count_; }
    void flush() { ++generation_; count_ = 0; }
    unsigned count() const { return count_; }
    std::uint64_t generation() const { return generation_; }
private:
    unsigned count_ = 0;
    std::uint64_t generation_ = 1;
};
}
