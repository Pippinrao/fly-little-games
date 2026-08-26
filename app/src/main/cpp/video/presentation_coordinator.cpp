#include "presentation_coordinator.h"

namespace flynes::video {

bool PresentationCoordinator::begin_create(std::uint64_t epoch) {
    if (epoch == 0 || epoch <= epoch_) return false;
    epoch_ = epoch;
    state_ = State::CREATING;
    return true;
}

void PresentationCoordinator::complete_create(std::uint64_t epoch, bool ready) {
    if (epoch == epoch_ && state_ == State::CREATING) {
        state_ = ready ? State::READY : State::EMPTY;
    }
}

bool PresentationCoordinator::begin_destroy(std::uint64_t epoch) {
    if (epoch != epoch_ || (state_ != State::READY && state_ != State::CREATING)) return false;
    state_ = State::DESTROYING;
    return true;
}

bool PresentationCoordinator::is_active(std::uint64_t epoch) const {
    return epoch == epoch_ && state_ == State::READY;
}

}  // namespace flynes::video
