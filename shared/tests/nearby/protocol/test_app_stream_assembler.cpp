#include "app_stream_assembler.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using flynes::session::wire::AppStreamAssembler;
using flynes::session::wire::OwnedAppFrame;
using flynes::session::wire::QuicChannel;
using flynes::session::wire::StreamAssemblyResult;

namespace {

namespace fs = std::filesystem;
int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<std::uint8_t> read_bytes(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> control_frame()
{
    const auto body = read_bytes(fs::path(FLYNES_SESSION_GOLDEN_DIR) /
                                 "suspend_intent_v1" / "legal.bin");
    std::vector<std::uint8_t> frame(body.size() + 6);
    std::size_t written = 0;
    check(flynes::session::wire::encode_app_frame(0x0210, body.data(), body.size(),
                                                   frame.data(), frame.size(), &written) ==
              flynes::session::wire::Status::Ok && written == frame.size(),
          "valid Control frame fixture");
    return frame;
}

void test_every_split_and_coalescing_point()
{
    const auto one = control_frame();
    std::vector<std::uint8_t> two = one;
    two.insert(two.end(), one.begin(), one.end());
    for (std::size_t split = 0; split <= two.size(); ++split)
    {
        AppStreamAssembler stream(QuicChannel::Control);
        std::vector<OwnedAppFrame> output;
        const auto first = stream.data(two.data(), split, &output);
        check(first == (split == two.size() ? StreamAssemblyResult::Produced
                                            : StreamAssemblyResult::NeedMore) ||
                  first == StreamAssemblyResult::Produced,
              "arbitrary first split accepted");
        const auto second = stream.data(two.data() + split, two.size() - split, &output);
        check(second == StreamAssemblyResult::Produced ||
                  (split == two.size() && second == StreamAssemblyResult::NeedMore),
              "arbitrary second split accepted");
        check(stream.finish(&output) == StreamAssemblyResult::CleanFin,
              "FIN at record boundary accepted");
        check(output.size() == 2 && output[0].object_bytes == output[1].object_bytes,
              "coalesced records each emitted exactly once");
    }

    AppStreamAssembler bytewise(QuicChannel::Control);
    std::vector<OwnedAppFrame> output;
    for (std::uint8_t byte : two)
        check(bytewise.data(&byte, 1, &output) == StreamAssemblyResult::NeedMore ||
                  !output.empty(), "single-byte chunks accepted");
    check(bytewise.finish(&output) == StreamAssemblyResult::CleanFin && output.size() == 2,
          "single-byte delivery yields both records");
}

void test_streams_have_independent_cursors()
{
    const auto frame = control_frame();
    AppStreamAssembler left(QuicChannel::Control);
    AppStreamAssembler right(QuicChannel::Control);
    std::vector<OwnedAppFrame> left_out;
    std::vector<OwnedAppFrame> right_out;
    const std::size_t split = frame.size() / 2;
    check(left.data(frame.data(), split, &left_out) == StreamAssemblyResult::NeedMore,
          "left holds its own half frame");
    check(right.data(frame.data(), frame.size(), &right_out) == StreamAssemblyResult::Produced &&
              right_out.size() == 1,
          "right completes while left remains partial");
    check(left.data(frame.data() + split, frame.size() - split, &left_out) ==
              StreamAssemblyResult::Produced && left_out.size() == 1,
          "left later completes without shared cursor state");
}

void test_fin_reset_and_preallocation_bounds()
{
    const auto frame = control_frame();
    for (const std::size_t partial : {std::size_t{1}, std::size_t{3}, std::size_t{4},
                                      frame.size() - 1})
    {
        AppStreamAssembler stream(QuicChannel::Control);
        std::vector<OwnedAppFrame> output;
        check(stream.data(frame.data(), partial, &output) == StreamAssemblyResult::NeedMore,
              "partial prefix/body retained");
        check(stream.finish(&output) == StreamAssemblyResult::ProtocolViolation,
              "FIN with a partial prefix/body is terminal");
        check(stream.data(frame.data(), frame.size(), &output) == StreamAssemblyResult::Closed,
              "terminal stream rejects later DATA");
    }

    AppStreamAssembler reset(QuicChannel::Control);
    std::vector<OwnedAppFrame> output;
    check(reset.data(frame.data(), 5, &output) == StreamAssemblyResult::NeedMore,
          "RESET fixture buffers prefix");
    check(reset.reset() == StreamAssemblyResult::Closed && reset.buffered_bytes() == 0,
          "RESET drops owned partial bytes and closes stream");

    const std::uint8_t oversized_prefix[4]{0x00, 0x01, 0x00, 0x03}; // 65539 > 2+65536.
    AppStreamAssembler oversized(QuicChannel::Control);
    check(oversized.data(oversized_prefix, sizeof(oversized_prefix), &output) ==
              StreamAssemblyResult::Backpressure,
          "oversized prefix rejected before body allocation");
    check(oversized.buffered_bytes() == 0,
          "oversized declaration retains no attacker-sized buffer");
}

} // namespace

int main()
{
    test_every_split_and_coalescing_point();
    test_streams_have_independent_cursors();
    test_fin_reset_and_preallocation_bounds();
    if (failures != 0)
        return 1;
    std::cout << "incremental app stream assembler passed\n";
    return 0;
}
