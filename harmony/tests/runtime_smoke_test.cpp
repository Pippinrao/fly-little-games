#include "runtime_smoke.hpp"

#include <flynes/flynes_runtime.h>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Function>
std::string capture_error(Function&& function)
{
    try
    {
        function();
    }
    catch (const std::exception& error)
    {
        return error.what();
    }
    catch (...)
    {
        return "non-standard exception";
    }

    return {};
}

void test_runtime_smoke_steps_one_ntsc_frame_and_round_trips_checkpoint()
{
    const flynes::harmony::RuntimeSmokeResult result =
        flynes::harmony::run_runtime_smoke(FLYNES_HARMONY_RUNTIME_ROM_FIXTURE);

    expect(result.frame_width == "256", "copied frame width must be 256");
    expect(result.frame_height == "240", "copied frame height must be 240");
    expect(result.format == "1", "copied frame format must be RGB565");
    expect(result.bytes_written == "122880",
           "copied frame must be a complete 256x240 RGB565 buffer");
    expect(result.pcm_sample_count != "0", "bounded PCM pull must return produced samples");
    expect(result.checkpoint_ok == "1", "checkpoint save/load must round-trip");
}

void test_missing_rom_is_rejected_before_create()
{
    const std::string message = capture_error([] {
        (void)flynes::harmony::run_runtime_smoke({});
    });

    expect(message.find("validate rom path") != std::string::npos,
           "empty ROM path must identify its validation step");
    expect(message.find("fly_result=") != std::string::npos,
           "validation failure must include the fly_result code");
}

void test_unreadable_rom_does_not_echo_path_bytes()
{
    const std::string message = capture_error([] {
        (void)flynes::harmony::run_runtime_smoke("secret-rom-path.nes");
    });

    expect(message.find("read rom") != std::string::npos,
           "missing ROM must identify the read step");
    expect(message.find("secret-rom-path") == std::string::npos,
           "native errors must not echo ROM path bytes");
}

} // namespace

static_assert(FLY_RUNTIME_PIXEL_FORMAT_RGB565 == 1, "RGB565 format value");
static_assert(FLY_RUNTIME_RGB565_BYTES == 122880u, "RGB565 complete-frame size");

int main()
{
    test_runtime_smoke_steps_one_ntsc_frame_and_round_trips_checkpoint();
    test_missing_rom_is_rejected_before_create();
    test_unreadable_rom_does_not_echo_path_bytes();

    if (failures != 0)
    {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "PASS: Harmony runtime smoke contract\n";
    return EXIT_SUCCESS;
}
