#pragma once

#include <cstdint>

namespace flynes::video {

struct Rgb565FrameView {
    const std::uint16_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int stride_pixels = 0;
};

struct MutableRgb565FrameView {
    std::uint16_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int stride_pixels = 0;
};

enum class TemporalHoldReason {
    NONE,
    INVALID_INPUT,
    SCENE_CUT,
    FULL_FLASH,
    UNSAFE_CONFIDENCE
};

struct TemporalInterpolationConfig {
    int block_size = 4;
    int coarse_block_quarter = 8;
    int coarse_search_radius_quarter = 8;
    int refinement_radius = 2;
    double scene_cut_ratio = 0.45;
    double full_flash_ratio = 0.80;
    double unsafe_ratio_limit = 0.10;
};

struct TemporalInterpolationResult {
    bool generated = false;
    TemporalHoldReason reason = TemporalHoldReason::INVALID_INPUT;
    double changed_ratio = 0.0;
    double unsafe_ratio = 0.0;
    std::uint64_t moved_blocks = 0;
    std::uint64_t held_blocks = 0;
};

/**
 * Portable, deterministic RGB565 midpoint oracle.
 *
 * It never blends colors: output samples only A or B. Ambiguous motion holds A,
 * and global discontinuities fail closed. The Android compute implementation is
 * required to match this contract before it may be enabled.
 */
class TemporalInterpolator {
public:
    explicit TemporalInterpolator(TemporalInterpolationConfig config = {});

    TemporalInterpolationResult interpolate_midpoint(
            Rgb565FrameView a, Rgb565FrameView b, MutableRgb565FrameView output) const;

private:
    TemporalInterpolationConfig config_;
};

}  // namespace flynes::video
