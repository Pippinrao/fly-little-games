#include "lan_mvp/lockstep.hpp"
#include "lan_mvp/wire.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
}

int main() {
    using namespace flynes::session::lan_mvp;

    std::vector<std::uint8_t> first;
    std::vector<std::uint8_t> second;
    check(wire::encode(wire::Kind::Config, {1, 2, 3}, &first), "encode config");
    check(wire::encode(wire::Kind::Ready, {4}, &second), "encode ready");

    wire::Decoder split;
    check(split.push(first.data(), 1), "accept split prefix");
    wire::Message message{};
    check(!split.pop(&message), "partial prefix does not emit");
    check(split.push(first.data() + 1, first.size() - 1), "accept split body");
    check(split.pop(&message) && message.kind == wire::Kind::Config &&
          message.payload == std::vector<std::uint8_t>({1, 2, 3}),
          "fragmented message round trips");

    std::vector<std::uint8_t> joined = first;
    joined.insert(joined.end(), second.begin(), second.end());
    wire::Decoder coalesced;
    check(coalesced.push(joined.data(), joined.size()), "accept coalesced messages");
    check(coalesced.pop(&message) && message.kind == wire::Kind::Config,
          "first coalesced message");
    check(coalesced.pop(&message) && message.kind == wire::Kind::Ready,
          "second coalesced message");
    check(!coalesced.pop(&message), "coalesced buffer drained");

    const std::uint8_t invalid[] = {0xff, 0xff, 1};
    wire::Decoder rejected;
    check(!rejected.push(invalid, sizeof(invalid)) && rejected.failed(),
          "oversize length is rejected");

    lockstep::Buffer inputs(2, 8);
    check(inputs.put_local(0, 0) == lockstep::Put::Accepted, "zero frame local");
    check(inputs.put_remote(0, 0) == lockstep::Put::Accepted, "zero frame remote");
    lockstep::Frame frame{};
    check(inputs.pop_ready(&frame) && frame.index == 0 && frame.local_mask == 0 &&
          frame.remote_mask == 0, "complete frame advances once");
    check(!inputs.pop_ready(&frame), "completed frame does not repeat");
    check(inputs.put_local(2, 0x01) == lockstep::Put::Accepted, "buffered local input");
    check(!inputs.pop_ready(&frame), "one side cannot advance");
    check(inputs.put_remote(2, 0x80) == lockstep::Put::Accepted, "buffered remote input");
    check(inputs.put_remote(2, 0x80) == lockstep::Put::Duplicate, "duplicate is idempotent");
    check(inputs.put_remote(2, 0x40) == lockstep::Put::Conflict, "conflicting duplicate rejected");
    check(inputs.put_local(0, 0) == lockstep::Put::Late, "completed input is late");
    check(inputs.put_local(20, 0) == lockstep::Put::OutOfWindow, "far input is bounded");
    check(inputs.put_local(1, 0) == lockstep::Put::Accepted &&
          inputs.put_remote(1, 0) == lockstep::Put::Accepted,
          "missing current frame can arrive after future frame");
    check(inputs.pop_ready(&frame) && frame.index == 1, "current frame advances");
    check(inputs.pop_ready(&frame) && frame.index == 2 && frame.local_mask == 0x01 &&
          frame.remote_mask == 0x80, "future frame advances exactly once");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
