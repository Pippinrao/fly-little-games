#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace flynes::ios {
inline std::vector<std::uint8_t> rgb565_to_rgba8(const std::uint8_t* bytes, std::size_t length, int width, int height) {
    if (bytes == nullptr || width <= 0 || height <= 0 || width > 4096 || height > 4096) return {};
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (count > length / 2) return {};
    std::vector<std::uint8_t> output(count * 4);
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned value = bytes[i * 2] | (static_cast<unsigned>(bytes[i * 2 + 1]) << 8);
        output[i * 4] = static_cast<std::uint8_t>((((value >> 11) & 31) * 255 + 15) / 31);
        output[i * 4 + 1] = static_cast<std::uint8_t>((((value >> 5) & 63) * 255 + 31) / 63);
        output[i * 4 + 2] = static_cast<std::uint8_t>(((value & 31) * 255 + 15) / 31);
        output[i * 4 + 3] = 255;
    }
    return output;
}
}
