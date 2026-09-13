#include "metal/Rgb565Upload.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    try {
        std::vector<std::uint8_t> input(65536 * 2);
        for (unsigned n = 0; n < 65536; ++n) { input[n * 2] = n & 255; input[n * 2 + 1] = n >> 8; }
        const auto output = flynes::ios::rgb565_to_rgba8(input.data(), input.size(), 256, 256);
        if (output.size() != 65536 * 4) throw std::runtime_error("wrong upload size");
        for (unsigned n = 0; n < 65536; ++n) {
            const unsigned expected[] = {
                static_cast<unsigned>(std::lround(((n >> 11) & 31) * (255.0 / 31.0))),
                static_cast<unsigned>(std::lround(((n >> 5) & 63) * (255.0 / 63.0))),
                static_cast<unsigned>(std::lround((n & 31) * (255.0 / 31.0))), 255};
            for (unsigned c = 0; c < 4; ++c)
                if (output[n * 4 + c] != expected[c]) throw std::runtime_error("RGB565 channel conversion mismatch");
        }
        if (!flynes::ios::rgb565_to_rgba8(input.data(), 1, 1, 1).empty()) throw std::runtime_error("truncated input accepted");
        if (!flynes::ios::rgb565_to_rgba8(nullptr, 2, 1, 1).empty()) throw std::runtime_error("null input accepted");
        if (!flynes::ios::rgb565_to_rgba8(input.data(), input.size(), -1, 1).empty()) throw std::runtime_error("negative size accepted");
        std::cout << "ios_rgb565_upload: PASS\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
