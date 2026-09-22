#include "play_session.hpp"

#include <flynes/flynes_runtime.h>

#include <array>

namespace flynes::harmony {
namespace {

class RuntimeDeleter final
{
public:
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
        throw PlaySessionError(step, static_cast<int>(result));
    }
}

fly_frame_input_v1 make_input(std::uint64_t epoch,
                              std::uint64_t frame_index,
                              std::uint32_t port0_buttons,
                              std::uint64_t input_sequence)
{
    fly_frame_input_v1 input{};
    input.struct_size = FLY_FRAME_INPUT_V1_SIZE;
    input.version = FLY_FRAME_INPUT_VERSION_1;
    input.timeline_epoch = epoch;
    input.frame_index = frame_index;
    input.buttons[0] = port0_buttons;
    input.input_sequence[0] = input_sequence;
    input.input_sequence[1] = 1;
    input.input_sequence[2] = 1;
    input.input_sequence[3] = 1;
    return input;
}

} // namespace

PlaySessionError::PlaySessionError(const char* step, int result)
    : std::runtime_error(std::string(step) + " failed: fly_result=" + std::to_string(result))
{
}

struct PlaySession::Impl final
{
    RuntimeHandle runtime;
    std::function<PlayStepResult(std::uint32_t)> frame_source;
    PlayStepResult latest;
    std::uint32_t port0_buttons = 0;
    std::uint64_t timeline_epoch = 1;
    std::uint64_t next_frame_index = 0;
    std::uint64_t input_sequence = 1;

    PlayStepResult copy_latest_frame(bool pull_pcm)
    {
        PlayStepResult result{};
        result.rgb565.assign(FLY_RUNTIME_RGB565_BYTES, 0);
        fly_latest_frame_v1 meta{};
        meta.struct_size = FLY_LATEST_FRAME_V1_SIZE;
        meta.version = FLY_LATEST_FRAME_VERSION_1;
        require_ok(fly_runtime_copy_latest_frame(
                       runtime.get(), result.rgb565.data(), result.rgb565.size(), &meta),
                   "copy latest frame");
        result.frame_index = meta.frame_index;
        result.width = meta.width;
        result.height = meta.height;
        result.format = meta.format;
        result.bytes_written = meta.bytes_written;
        result.applied_buttons = port0_buttons;

        if (pull_pcm)
        {
            std::array<std::int16_t, 4096> samples{};
            fly_pcm_block_v1 block{};
            block.struct_size = FLY_PCM_BLOCK_V1_SIZE;
            block.version = FLY_PCM_BLOCK_VERSION_1;
            require_ok(fly_runtime_pull_pcm(runtime.get(), samples.data(),
                                            static_cast<std::uint32_t>(samples.size()), &block),
                       "pull pcm");
            result.pcm_sample_count = block.sample_count;
            result.pcm.assign(samples.begin(), samples.begin() + block.sample_count);
        }
        return result;
    }
};

PlaySession::PlaySession() : impl_(std::make_unique<Impl>()) {}

PlaySession::~PlaySession() = default;

std::unique_ptr<PlaySession> PlaySession::from_frame_source(
    std::function<PlayStepResult(std::uint32_t)> source)
{
    if (!source) throw PlaySessionError("frame source", FLY_RESULT_INVALID_ARGUMENT);
    auto session = std::unique_ptr<PlaySession>(new PlaySession());
    session->impl_->frame_source = std::move(source);
    return session;
}

std::unique_ptr<PlaySession> PlaySession::open(const std::uint8_t* rom, std::size_t size)
{
    if (rom == nullptr || size == 0)
    {
        throw PlaySessionError("validate rom", FLY_RESULT_INVALID_ARGUMENT);
    }

    auto session = std::unique_ptr<PlaySession>(new PlaySession());
    fly_runtime_config config{};
    config.struct_size = FLY_RUNTIME_CONFIG_V1_SIZE;
    config.version = FLY_RUNTIME_CONFIG_VERSION_1;
    config.sample_rate = 0;
    config.reserved = 0;

    fly_runtime_t* raw = nullptr;
    require_ok(fly_runtime_create(&config, &raw), "fly_runtime_create");
    if (raw == nullptr)
    {
        throw PlaySessionError("fly_runtime_create invariant", FLY_RESULT_INTERNAL_ERROR);
    }
    session->impl_->runtime.reset(raw);
    require_ok(fly_runtime_load_rom(session->impl_->runtime.get(), rom, size, nullptr),
               "fly_runtime_load_rom");
    return session;
}

void PlaySession::set_port0_buttons(std::uint32_t buttons)
{
    impl_->port0_buttons = buttons;
}

PlayStepResult PlaySession::step()
{
    if (impl_->frame_source) {
        auto result = impl_->frame_source(impl_->port0_buttons);
        impl_->latest = result;
        impl_->latest.pcm.clear();
        impl_->latest.pcm_sample_count = 0;
        return result;
    }
    const fly_frame_input_v1 input = make_input(impl_->timeline_epoch, impl_->next_frame_index,
                                                impl_->port0_buttons, impl_->input_sequence);
    fly_frame_result_v1 frame{};
    frame.struct_size = FLY_FRAME_RESULT_V1_SIZE;
    frame.version = FLY_FRAME_RESULT_VERSION_1;
    require_ok(fly_runtime_step_frame(impl_->runtime.get(), &input, &frame),
               "fly_runtime_step_frame");
    ++impl_->next_frame_index;
    ++impl_->input_sequence;
    PlayStepResult result = impl_->copy_latest_frame(true);
    result.applied_buttons = impl_->port0_buttons;
    return result;
}

PlayStepResult PlaySession::copy_latest_frame()
{
    if (impl_->frame_source) return impl_->latest;
    PlayStepResult result = impl_->copy_latest_frame(false);
    result.applied_buttons = impl_->port0_buttons;
    return result;
}

std::vector<std::uint8_t> PlaySession::save_checkpoint()
{
    std::size_t written = 0;
    std::size_t needed = 0;
    const fly_result query =
        fly_runtime_save_checkpoint(impl_->runtime.get(), nullptr, 0, &written, &needed);
    if (query != FLY_RESULT_BUFFER_TOO_SMALL || needed == 0)
    {
        throw PlaySessionError("save checkpoint",
                               query == FLY_RESULT_OK ? FLY_RESULT_INTERNAL_ERROR : query);
    }
    std::vector<std::uint8_t> bytes(needed);
    written = 0;
    require_ok(fly_runtime_save_checkpoint(
                   impl_->runtime.get(), bytes.data(), bytes.size(), &written, &needed),
               "save checkpoint");
    bytes.resize(written);
    return bytes;
}

void PlaySession::load_checkpoint(const std::uint8_t* bytes, std::size_t size)
{
    if (bytes == nullptr || size == 0)
    {
        throw PlaySessionError("load checkpoint", FLY_RESULT_INVALID_ARGUMENT);
    }
    require_ok(fly_runtime_load_checkpoint(impl_->runtime.get(), bytes, size),
               "load checkpoint");
    const PlayStepResult restored = impl_->copy_latest_frame(false);
    impl_->next_frame_index = restored.frame_index + 1;
}

} // namespace flynes::harmony
