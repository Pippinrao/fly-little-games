#pragma once

#include <cmath>
#include <cstdint>

inline uint32_t nes_samples_for_next_frame(uint32_t sample_rate, double fps,
                                           double& remainder)
{
    const double exact = static_cast<double>(sample_rate) / fps + remainder;
    const uint32_t whole = static_cast<uint32_t>(std::floor(exact));
    remainder = exact - whole;
    return whole;
}
