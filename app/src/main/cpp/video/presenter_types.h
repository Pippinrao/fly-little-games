#pragma once

#include <cstdint>
#include <vector>

namespace flynes::video {

enum class PixelFormat : int { RGB565 = 0, RGB888 = 1, RGBA8888 = 2 };
enum class FilterMode : int {
    EDGE_ENHANCED = 0, SHARP_BILINEAR = 1, NEAREST = 2, CRT = 3, MMPX = 4, SCALEFX = 5
};

struct StagedFrame {
    std::uint64_t sequence = 0;
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::RGB565;
    std::vector<std::uint8_t> pixels;
};

}  // namespace flynes::video
