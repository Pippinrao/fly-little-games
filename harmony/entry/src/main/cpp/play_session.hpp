#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace flynes::harmony {

class PlaySessionError final : public std::runtime_error
{
public:
    PlaySessionError(const char* step, int result);
};

struct PlayStepResult final
{
    std::uint64_t frame_index = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t format = 0;
    std::uint32_t bytes_written = 0;
    std::uint32_t pcm_sample_count = 0;
    std::uint32_t applied_buttons = 0;
    std::vector<std::uint8_t> rgb565;
    std::vector<std::int16_t> pcm;
};

class PlaySession final
{
public:
    static std::unique_ptr<PlaySession> open(const std::uint8_t* rom, std::size_t size);
    // Reuse the normal renderer/audio runtime with an already running session.
    static std::unique_ptr<PlaySession> from_frame_source(
        std::function<PlayStepResult(std::uint32_t)> source);

    PlaySession(const PlaySession&) = delete;
    PlaySession& operator=(const PlaySession&) = delete;
    ~PlaySession();

    void set_port0_buttons(std::uint32_t buttons);
    PlayStepResult step();
    PlayStepResult copy_latest_frame();
    std::vector<std::uint8_t> save_checkpoint();
    void load_checkpoint(const std::uint8_t* bytes, std::size_t size);

private:
    PlaySession();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace flynes::harmony
