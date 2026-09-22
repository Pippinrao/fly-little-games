#include "lan_mvp/lockstep.hpp"

namespace flynes::session::lan_mvp::lockstep {

Buffer::Buffer(std::size_t, std::size_t window) : window_(window) {}

Put Buffer::put(std::uint64_t index, std::uint32_t mask, bool local) {
    if (index < next_frame_) return Put::Late;
    const auto distance = index - next_frame_;
    if (distance >= window_) return Put::OutOfWindow;

    auto& slot = slots_[index];
    auto& value = local ? slot.local : slot.remote;
    if (!value.has_value()) {
        value = mask;
        return Put::Accepted;
    }
    return value.value() == mask ? Put::Duplicate : Put::Conflict;
}

Put Buffer::put_local(std::uint64_t index, std::uint32_t mask) {
    return put(index, mask, true);
}

Put Buffer::put_remote(std::uint64_t index, std::uint32_t mask) {
    return put(index, mask, false);
}

bool Buffer::pop_ready(Frame* frame) {
    if (frame == nullptr) return false;
    const auto found = slots_.find(next_frame_);
    if (found == slots_.end() || !found->second.local.has_value() ||
        !found->second.remote.has_value()) {
        return false;
    }
    frame->index = next_frame_;
    frame->local_mask = found->second.local.value();
    frame->remote_mask = found->second.remote.value();
    slots_.erase(found);
    ++next_frame_;
    return true;
}

} // namespace flynes::session::lan_mvp::lockstep
