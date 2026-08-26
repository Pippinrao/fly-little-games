#pragma once

#include <cstdint>

namespace flynes::video {

/** Typed surface lifecycle. Old-epoch callbacks cannot mutate the active surface. */
class PresentationCoordinator {
public:
    bool begin_create(std::uint64_t epoch);
    void complete_create(std::uint64_t epoch, bool ready);
    bool begin_destroy(std::uint64_t epoch);
    bool is_active(std::uint64_t epoch) const;
    std::uint64_t epoch() const { return epoch_; }

private:
    enum class State { EMPTY, CREATING, READY, DESTROYING };
    State state_ = State::EMPTY;
    std::uint64_t epoch_ = 0;
};

}  // namespace flynes::video
