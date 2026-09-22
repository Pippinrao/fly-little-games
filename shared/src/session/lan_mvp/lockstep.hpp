#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>

namespace flynes::session::lan_mvp::lockstep {

enum class Put { Accepted, Duplicate, Conflict, Late, OutOfWindow };

struct Frame {
    std::uint64_t index = 0;
    std::uint32_t local_mask = 0;
    std::uint32_t remote_mask = 0;
};

class Buffer {
public:
    Buffer(std::size_t input_delay, std::size_t window);

    Put put_local(std::uint64_t index, std::uint32_t mask);
    Put put_remote(std::uint64_t index, std::uint32_t mask);
    bool pop_ready(Frame* frame);
    std::uint64_t next_frame() const { return next_frame_; }

private:
    struct Slot {
        std::optional<std::uint32_t> local;
        std::optional<std::uint32_t> remote;
    };

    Put put(std::uint64_t index, std::uint32_t mask, bool local);

    std::size_t window_;
    std::uint64_t next_frame_ = 0;
    std::map<std::uint64_t, Slot> slots_;
};

} // namespace flynes::session::lan_mvp::lockstep
