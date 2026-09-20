#include <cstdio>
#if __has_include("content_port.hpp")
#include "content_port.hpp"
#include <future>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

using flynes::android::nearby::ContentPort;
namespace {
int failures = 0;
void check(bool ok, const char* message) { if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); } }
std::vector<std::uint8_t> row() {
    std::vector<std::uint8_t> bytes(57, 0);
    bytes[1] = 1; bytes[4] = 1; bytes[20] = 2; bytes[55] = 1; bytes[56] = 'A';
    return bytes;
}
}
int main() {
    std::vector<std::uint8_t> output;
    auto input = row();
    check(ContentPort::encode_record(input, &output) == FLY_SESSION_V2_OK && output.size() == 153 && output[1] == 2,
          "canonical v1 metadata receives shared v2 identity");
    for (auto malformed : {std::vector<std::uint8_t>{}, std::vector<std::uint8_t>(56, 0)})
        check(ContentPort::encode_record(malformed, &output) == FLY_SESSION_V2_INVALID_ARGUMENT, "short/empty metadata rejected");
    for (std::size_t offset : {std::size_t(0), std::size_t(2), std::size_t(52), std::size_t(56)}) {
        auto bad = input; bad[offset] = 0xFF;
        check(ContentPort::encode_record(bad, &output) == FLY_SESSION_V2_INVALID_ARGUMENT, "malformed metadata rejected");
    }
    auto zero = input; zero[4] = 0;
    check(ContentPort::encode_record(zero, &output) == FLY_SESSION_V2_INVALID_ARGUMENT, "zero opaque reference rejected");
    zero = input; zero[20] = 0;
    check(ContentPort::encode_record(zero, &output) == FLY_SESSION_V2_INVALID_ARGUMENT, "zero content hash rejected");
    check(ContentPort::encode_record(input, nullptr) == FLY_SESSION_V2_INVALID_ARGUMENT, "null output rejected");
    std::atomic<int> closed{0};
    auto owner = std::make_unique<ContentPort>(ContentPort::Callbacks{
        [](std::uint32_t, std::vector<std::uint8_t>*) { return FLY_SESSION_V2_EMPTY; },
        [](const std::uint8_t*) { return false; }, [&] { ++closed; }});
    auto table = owner->port(); table.retain(table.context);
    check(table.query(table.context, nullptr, 0, nullptr) == FLY_SESSION_V2_INVALID_ARGUMENT, "null query rejected");
    check(table.cancel(table.context, nullptr) == FLY_SESSION_V2_INVALID_ARGUMENT, "null cancel rejected");
    owner.reset();
    check(closed == 1, "provider closes exactly once before retained context release");
    check(!ContentPort::validate(table, input.data() + 4), "retained provider cannot authorize after owner destruction");
    table.release(table.context);
    check(closed == 1, "retained table release does not repeat close callback");
    std::promise<void> entered, finish, closing;
    auto finish_signal = finish.get_future().share();
    auto raced = std::make_unique<ContentPort>(ContentPort::Callbacks{
        {}, [&](const std::uint8_t*) { entered.set_value(); finish_signal.wait(); return true; },
        [&] { ++closed; }});
    auto retained = raced->port(); retained.retain(retained.context);
    auto validation = std::async(std::launch::async, [&] { return ContentPort::validate(retained, input.data() + 4); });
    entered.get_future().wait();
    auto destruction = std::async(std::launch::async, [&] { closing.set_value(); raced.reset(); });
    closing.get_future().wait();
    check(destruction.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout && closed == 1,
          "close waits for the in-flight metadata callback barrier");
    finish.set_value();
    check(validation.get(), "in-flight callback finishes before context closes");
    destruction.get();
    check(!ContentPort::validate(retained, input.data() + 4) && closed == 2,
          "late callback after close cannot authorize or touch released Java state");
    retained.release(retained.context);
    return failures ? 1 : 0;
}
#else
int main() { std::fputs("FAIL: production Android ContentPort is absent\n", stderr); return 1; }
#endif
