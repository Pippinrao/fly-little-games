#pragma once

#include <string>
#include <string_view>

namespace flynes::harmony {

struct RuntimeSmokeResult final
{
    std::string frame_width;
    std::string frame_height;
    std::string format;
    std::string bytes_written;
    std::string pcm_sample_count;
    std::string checkpoint_ok;
};

[[nodiscard]] RuntimeSmokeResult run_runtime_smoke(std::string_view rom_path);

} // namespace flynes::harmony
