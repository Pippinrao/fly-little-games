#include "runtime_smoke.hpp"

#include <flynes/flynes_runtime.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace flynes::harmony {
namespace {

class RuntimeSmokeError final : public std::runtime_error
{
public:
    RuntimeSmokeError(const char* step, fly_result result)
        : std::runtime_error(std::string(step) + " failed: fly_result=" +
                             std::to_string(static_cast<int>(result)))
    {
    }
};

struct RuntimeDeleter final
{
    void operator()(fly_runtime_t* runtime) const noexcept
    {
        fly_runtime_destroy(runtime);
    }
};

using RuntimeHandle = std::unique_ptr<fly_runtime_t, RuntimeDeleter>;

void require_ok(fly_result result, const char* step)
{
    if (result != FLY_RESULT_OK)
    {
        throw RuntimeSmokeError(step, result);
    }
}

std::string decimal_string(std::uint64_t value)
{
    std::array<char, 32> buffer{};
    const auto conversion = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (conversion.ec != std::errc{})
    {
        throw RuntimeSmokeError("format runtime value", FLY_RESULT_INTERNAL_ERROR);
    }
    return std::string(buffer.data(), conversion.ptr);
}

std::vector<std::uint8_t> read_rom_bytes(std::string_view rom_path)
{
    if (rom_path.empty())
    {
        throw RuntimeSmokeError("validate rom path", FLY_RESULT_INVALID_ARGUMENT);
    }

    std::ifstream input(std::string(rom_path), std::ios::binary);
    if (!input)
    {
        throw RuntimeSmokeError("read rom", FLY_RESULT_INVALID_ARGUMENT);
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0)
    {
        throw RuntimeSmokeError("read rom", FLY_RESULT_INVALID_ARGUMENT);
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input)
    {
        throw RuntimeSmokeError("read rom", FLY_RESULT_INVALID_ARGUMENT);
    }
    return bytes;
}

fly_frame_input_v1 zero_input()
{
    fly_frame_input_v1 input{};
    input.struct_size = FLY_FRAME_INPUT_V1_SIZE;
    input.version = FLY_FRAME_INPUT_VERSION_1;
    input.timeline_epoch = 1;
    input.frame_index = 0;
    input.buttons[0] = 0;
    input.buttons[1] = 0;
    input.buttons[2] = 0;
    input.buttons[3] = 0;
    input.input_sequence[0] = 1;
    input.input_sequence[1] = 1;
    input.input_sequence[2] = 1;
    input.input_sequence[3] = 1;
    return input;
}

std::vector<std::uint8_t> save_checkpoint(fly_runtime_t* runtime, const char* step)
{
    std::size_t written = 0;
    std::size_t needed = 0;
    const fly_result query =
        fly_runtime_save_checkpoint(runtime, nullptr, 0, &written, &needed);
    if (query != FLY_RESULT_BUFFER_TOO_SMALL || needed == 0)
    {
        throw RuntimeSmokeError(step, query == FLY_RESULT_OK ? FLY_RESULT_INTERNAL_ERROR : query);
    }
    std::vector<std::uint8_t> bytes(needed);
    written = 0;
    require_ok(fly_runtime_save_checkpoint(
                   runtime, bytes.data(), bytes.size(), &written, &needed),
               step);
    bytes.resize(written);
    return bytes;
}

} // namespace

RuntimeSmokeResult run_runtime_smoke(std::string_view rom_path)
{
    const std::vector<std::uint8_t> rom = read_rom_bytes(rom_path);

    fly_runtime_config config{};
    config.struct_size = FLY_RUNTIME_CONFIG_V1_SIZE;
    config.version = FLY_RUNTIME_CONFIG_VERSION_1;
    config.sample_rate = 0;
    config.reserved = 0;

    fly_runtime_t* raw = nullptr;
    require_ok(fly_runtime_create(&config, &raw), "fly_runtime_create");
    if (raw == nullptr)
    {
        throw RuntimeSmokeError("fly_runtime_create invariant", FLY_RESULT_INTERNAL_ERROR);
    }
    RuntimeHandle runtime(raw);

    require_ok(fly_runtime_load_rom(runtime.get(), rom.data(), rom.size(), nullptr),
               "fly_runtime_load_rom");

    const fly_frame_input_v1 input = zero_input();
    fly_frame_result_v1 frame{};
    frame.struct_size = FLY_FRAME_RESULT_V1_SIZE;
    frame.version = FLY_FRAME_RESULT_VERSION_1;
    require_ok(fly_runtime_step_frame(runtime.get(), &input, &frame), "fly_runtime_step_frame");

    std::vector<std::uint8_t> pixels(FLY_RUNTIME_RGB565_BYTES, 0xAAu);
    fly_latest_frame_v1 meta{};
    meta.struct_size = FLY_LATEST_FRAME_V1_SIZE;
    meta.version = FLY_LATEST_FRAME_VERSION_1;
    require_ok(fly_runtime_copy_latest_frame(
                   runtime.get(), pixels.data(), pixels.size(), &meta),
               "fly_runtime_copy_latest_frame");
    if (meta.width != FLY_RUNTIME_FRAME_WIDTH || meta.height != FLY_RUNTIME_FRAME_HEIGHT ||
        meta.format != FLY_RUNTIME_PIXEL_FORMAT_RGB565 ||
        meta.bytes_written != FLY_RUNTIME_RGB565_BYTES)
    {
        throw RuntimeSmokeError("complete rgb565 frame", FLY_RESULT_INTERNAL_ERROR);
    }

    std::array<std::int16_t, 2048> samples{};
    fly_pcm_block_v1 block{};
    block.struct_size = FLY_PCM_BLOCK_V1_SIZE;
    block.version = FLY_PCM_BLOCK_VERSION_1;
    require_ok(fly_runtime_pull_pcm(runtime.get(), samples.data(),
                                    static_cast<std::uint32_t>(samples.size()), &block),
               "fly_runtime_pull_pcm");

    const std::vector<std::uint8_t> checkpoint = save_checkpoint(runtime.get(), "save checkpoint");

    fly_runtime_t* raw_other = nullptr;
    require_ok(fly_runtime_create(&config, &raw_other), "fly_runtime_create replay");
    RuntimeHandle other(raw_other);
    require_ok(fly_runtime_load_rom(other.get(), rom.data(), rom.size(), nullptr),
               "fly_runtime_load_rom replay");
    require_ok(fly_runtime_load_checkpoint(other.get(), checkpoint.data(), checkpoint.size()),
               "fly_runtime_load_checkpoint");
    const std::vector<std::uint8_t> replayed =
        save_checkpoint(other.get(), "save checkpoint replay");
    if (replayed != checkpoint)
    {
        throw RuntimeSmokeError("checkpoint round-trip", FLY_RESULT_INTERNAL_ERROR);
    }

    runtime.reset();
    other.reset();

    return RuntimeSmokeResult{
        decimal_string(meta.width),
        decimal_string(meta.height),
        decimal_string(meta.format),
        decimal_string(meta.bytes_written),
        decimal_string(block.sample_count),
        "1",
    };
}

} // namespace flynes::harmony
