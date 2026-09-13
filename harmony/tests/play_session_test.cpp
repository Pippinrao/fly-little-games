#include "play_session.hpp"

#include <nes/nes.h>

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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

std::vector<std::uint8_t> read_rom(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return {};
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0)
    {
        return {};
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input)
    {
        return {};
    }
    return bytes;
}

bool pixels_equal(const std::vector<std::uint8_t>& left, const std::vector<std::uint8_t>& right)
{
    return left == right;
}

bool pixels_are_uniform(const std::vector<std::uint8_t>& pixels)
{
    if (pixels.size() < 2)
    {
        return true;
    }
    for (std::size_t index = 1; index < pixels.size(); ++index)
    {
        if (pixels[index] != pixels[0])
        {
            return false;
        }
    }
    return true;
}

void test_empty_rom_is_rejected()
{
    const std::string message = capture_error([] {
        (void)flynes::harmony::PlaySession::open(nullptr, 0);
    });
    expect(message.find("validate rom") != std::string::npos,
           "empty ROM must identify its validation step");
    expect(message.find("fly_result=") != std::string::npos,
           "validation failure must include the fly_result code");
}

void test_step_publishes_complete_frame_pcm_and_port0_buttons()
{
    const std::vector<std::uint8_t> rom = read_rom(FLYNES_HARMONY_RUNTIME_ROM_FIXTURE);
    expect(!rom.empty(), "thwaite.nes fixture is readable");
    if (rom.empty())
    {
        return;
    }

    auto session = flynes::harmony::PlaySession::open(rom.data(), rom.size());
    expect(session != nullptr, "open must return a session");
    if (session == nullptr)
    {
        return;
    }

    session->set_port0_buttons(NES_BTN_START);
    const flynes::harmony::PlayStepResult first = session->step();
    expect(first.frame_index == 0, "first stepped frame index must be 0");
    expect(first.width == 256, "frame width must be 256");
    expect(first.height == 240, "frame height must be 240");
    expect(first.format == 1, "frame format must be RGB565");
    expect(first.bytes_written == 122880, "frame must be a complete 256x240 RGB565 buffer");
    expect(first.rgb565.size() == 122880, "rgb565 payload size must match bytes_written");
    expect(first.pcm_sample_count != 0, "step must publish PCM samples");
    expect(first.pcm.size() == first.pcm_sample_count, "pcm payload size must match sample count");
    expect(first.applied_buttons == NES_BTN_START, "port-0 START must be applied");
    expect(!pixels_are_uniform(first.rgb565), "published frame must not be a uniform fill");

    const flynes::harmony::PlayStepResult second = session->step();
    expect(second.frame_index == 1, "second stepped frame index must be 1");
}

void test_checkpoint_restores_pixels_after_later_frames()
{
    const std::vector<std::uint8_t> rom = read_rom(FLYNES_HARMONY_RUNTIME_ROM_FIXTURE);
    expect(!rom.empty(), "thwaite.nes fixture is readable");
    if (rom.empty())
    {
        return;
    }

    auto session = flynes::harmony::PlaySession::open(rom.data(), rom.size());
    if (session == nullptr)
    {
        expect(false, "open must return a session");
        return;
    }

    flynes::harmony::PlayStepResult latest{};
    for (int index = 0; index < 30; ++index)
    {
        latest = session->step();
    }
    const std::vector<std::uint8_t> saved_pixels = latest.rgb565;
    const std::vector<std::uint8_t> checkpoint = session->save_checkpoint();
    expect(!checkpoint.empty(), "checkpoint must contain bytes");

    for (int index = 0; index < 30; ++index)
    {
        latest = session->step();
    }
    const std::vector<std::uint8_t> later_checkpoint = session->save_checkpoint();
    expect(later_checkpoint != checkpoint, "later frames must advance checkpoint bytes");

    session->load_checkpoint(checkpoint.data(), checkpoint.size());
    const flynes::harmony::PlayStepResult restored = session->copy_latest_frame();
    expect(pixels_equal(restored.rgb565, saved_pixels),
           "load_checkpoint must restore the saved picture");

    const flynes::harmony::PlayStepResult continued = session->step();
    expect(continued.frame_index == 30, "resume after load must keep the saved timeline");
}

} // namespace

int main()
{
    test_empty_rom_is_rejected();
    test_step_publishes_complete_frame_pcm_and_port0_buttons();
    test_checkpoint_restores_pixels_after_later_frames();

    if (failures != 0)
    {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "PASS: Harmony play session contract\n";
    return EXIT_SUCCESS;
}
