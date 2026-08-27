#include "temporal_interpolator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace flynes::video {
namespace {

struct Match {
    int dx = 0;
    int dy = 0;
    std::uint64_t best = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t second = std::numeric_limits<std::uint64_t>::max();
    int ties = 0;
};

int color_distance(std::uint16_t lhs, std::uint16_t rhs) {
    const int lr = (lhs >> 11) & 31;
    const int lg = (lhs >> 5) & 63;
    const int lb = lhs & 31;
    const int rr = (rhs >> 11) & 31;
    const int rg = (rhs >> 5) & 63;
    const int rb = rhs & 31;
    return std::abs(lr - rr) * 2 + std::abs(lg - rg) + std::abs(lb - rb) * 2;
}

int luma(std::uint16_t pixel) {
    const int r = ((pixel >> 11) & 31) * 255 / 31;
    const int g = ((pixel >> 5) & 63) * 255 / 63;
    const int b = (pixel & 31) * 255 / 31;
    return (54 * r + 183 * g + 19 * b) >> 8;
}

std::uint64_t block_cost(Rgb565FrameView from, int from_x, int from_y,
                         Rgb565FrameView to, int to_x, int to_y, int size) {
    std::uint64_t cost = 0;
    for (int y = 0; y < size; ++y) {
        const auto* from_row = from.pixels + (from_y + y) * from.stride_pixels;
        const auto* to_row = to.pixels + (to_y + y) * to.stride_pixels;
        for (int x = 0; x < size; ++x)
            cost += static_cast<std::uint64_t>(color_distance(from_row[from_x + x],
                                                              to_row[to_x + x]));
    }
    return cost;
}

Match search_refinement(Rgb565FrameView from, int x, int y, Rgb565FrameView to,
                        int size, int center_dx, int center_dy, int radius) {
    Match match;
    for (int offset_y = -radius; offset_y <= radius; ++offset_y) {
        for (int offset_x = -radius; offset_x <= radius; ++offset_x) {
            const int dx = center_dx + offset_x;
            const int dy = center_dy + offset_y;
            const int target_x = x + dx;
            const int target_y = y + dy;
            if (target_x < 0 || target_y < 0
                    || target_x + size > to.width || target_y + size > to.height) continue;
            const auto cost = block_cost(from, x, y, to, target_x, target_y, size);
            if (cost < match.best) {
                match.second = match.best;
                match.best = cost;
                match.dx = dx;
                match.dy = dy;
                match.ties = 1;
            } else if (cost == match.best) {
                ++match.ties;
            } else if (cost < match.second) {
                match.second = cost;
            }
        }
    }
    return match;
}

std::uint64_t quarter_block_cost(Rgb565FrameView from, int anchor_x, int anchor_y,
                                 Rgb565FrameView to, int dx_quarter, int dy_quarter,
                                 int block_quarter) {
    std::uint64_t cost = 0;
    const int dx = dx_quarter * 4;
    const int dy = dy_quarter * 4;
    for (int qy = 0; qy < block_quarter; ++qy) {
        for (int qx = 0; qx < block_quarter; ++qx) {
            const int x = anchor_x + qx * 4;
            const int y = anchor_y + qy * 4;
            cost += static_cast<std::uint64_t>(color_distance(
                    from.pixels[y * from.stride_pixels + x],
                    to.pixels[(y + dy) * to.stride_pixels + x + dx]));
        }
    }
    return cost;
}

Match hierarchical_search(Rgb565FrameView from, int x, int y, Rgb565FrameView to,
                          int block, int coarse_block_quarter,
                          int coarse_radius_quarter, int refinement_radius) {
    const int coarse_full = coarse_block_quarter * 4;
    const int anchor_x = (x / coarse_full) * coarse_full;
    const int anchor_y = (y / coarse_full) * coarse_full;
    int coarse_dx = 0;
    int coarse_dy = 0;
    if (anchor_x + coarse_full <= from.width && anchor_y + coarse_full <= from.height) {
        std::uint64_t best = std::numeric_limits<std::uint64_t>::max();
        for (int dyq = -coarse_radius_quarter; dyq <= coarse_radius_quarter; ++dyq) {
            for (int dxq = -coarse_radius_quarter; dxq <= coarse_radius_quarter; ++dxq) {
                const int tx = anchor_x + dxq * 4;
                const int ty = anchor_y + dyq * 4;
                if (tx < 0 || ty < 0 || tx + coarse_full > to.width
                        || ty + coarse_full > to.height) continue;
                const auto candidate = quarter_block_cost(from, anchor_x, anchor_y, to,
                        dxq, dyq, coarse_block_quarter);
                if (candidate < best) {
                    best = candidate;
                    coarse_dx = dxq * 4;
                    coarse_dy = dyq * 4;
                }
            }
        }
    }
    return search_refinement(from, x, y, to, block, coarse_dx, coarse_dy,
                             refinement_radius);
}

bool valid(Rgb565FrameView frame) {
    return frame.pixels && frame.width > 0 && frame.height > 0
            && frame.stride_pixels >= frame.width;
}

bool valid(MutableRgb565FrameView frame) {
    return frame.pixels && frame.width > 0 && frame.height > 0
            && frame.stride_pixels >= frame.width;
}

void copy_frame(Rgb565FrameView source, MutableRgb565FrameView output) {
    for (int y = 0; y < source.height; ++y) {
        std::copy_n(source.pixels + y * source.stride_pixels, source.width,
                    output.pixels + y * output.stride_pixels);
    }
}

}  // namespace

TemporalInterpolator::TemporalInterpolator(TemporalInterpolationConfig config)
        : config_(config) {
    if (config_.block_size <= 0) config_.block_size = 4;
    if (config_.coarse_block_quarter <= 0) config_.coarse_block_quarter = 8;
    if (config_.coarse_search_radius_quarter < 0)
        config_.coarse_search_radius_quarter = 0;
    if (config_.refinement_radius < 0) config_.refinement_radius = 0;
}

TemporalInterpolationResult TemporalInterpolator::interpolate_midpoint(
        Rgb565FrameView a, Rgb565FrameView b, MutableRgb565FrameView output) const {
    TemporalInterpolationResult result;
    if (!valid(a) || !valid(b) || !valid(output)
            || a.width != b.width || a.height != b.height
            || output.width != a.width || output.height != a.height) {
        return result;
    }
    copy_frame(a, output);
    const std::uint64_t pixel_count = static_cast<std::uint64_t>(a.width) * a.height;
    std::uint64_t changed = 0;
    std::uint64_t same_sign_brightness = 0;
    std::uint64_t large_brightness = 0;
    int brightness_sign = 0;
    for (int y = 0; y < a.height; ++y) {
        const auto* ar = a.pixels + y * a.stride_pixels;
        const auto* br = b.pixels + y * b.stride_pixels;
        for (int x = 0; x < a.width; ++x) {
            if (ar[x] == br[x]) continue;
            ++changed;
            const int delta = luma(br[x]) - luma(ar[x]);
            if (std::abs(delta) >= 96) ++large_brightness;
            const int sign = delta > 0 ? 1 : (delta < 0 ? -1 : 0);
            if (brightness_sign == 0 && sign != 0) brightness_sign = sign;
            if (sign != 0 && sign == brightness_sign) ++same_sign_brightness;
        }
    }
    result.changed_ratio = static_cast<double>(changed) / static_cast<double>(pixel_count);
    if (result.changed_ratio >= config_.full_flash_ratio
            && large_brightness * 10 >= changed * 9
            && same_sign_brightness * 10 >= changed * 9) {
        result.reason = TemporalHoldReason::FULL_FLASH;
        return result;
    }
    if (result.changed_ratio >= config_.scene_cut_ratio) {
        result.reason = TemporalHoldReason::SCENE_CUT;
        return result;
    }

    const int block = config_.block_size;
    std::uint64_t unsafe_pixels = 0;
    struct MotionBlock { int x; int y; int size; int dx; int dy; };
    std::vector<MotionBlock> motions;
    for (int y = 0; y + block <= a.height; y += block) {
        for (int x = 0; x + block <= a.width; x += block) {
            const auto zero_cost = block_cost(a, x, y, b, x, y, block);
            if (zero_cost == 0) continue;
            const Match forward = hierarchical_search(a, x, y, b, block,
                    config_.coarse_block_quarter,
                    config_.coarse_search_radius_quarter,
                    config_.refinement_radius);
            const int destination_x = x + forward.dx;
            const int destination_y = y + forward.dy;
            Match backward;
            if (destination_x >= 0 && destination_y >= 0
                    && destination_x + block <= b.width
                    && destination_y + block <= b.height) {
                backward = hierarchical_search(b, destination_x, destination_y, a, block,
                        config_.coarse_block_quarter,
                        config_.coarse_search_radius_quarter,
                        config_.refinement_radius);
            }
            const bool unique = forward.ties == 1 && backward.ties == 1;
            const bool reciprocal = std::abs(forward.dx + backward.dx) <= 1
                    && std::abs(forward.dy + backward.dy) <= 1;
            const bool useful = forward.best * 4 < zero_cost * 3;
            const bool separated = forward.second == std::numeric_limits<std::uint64_t>::max()
                    || forward.best + static_cast<std::uint64_t>(block * block * 2)
                            < forward.second;
            if (!unique || !reciprocal || !useful || !separated) {
                unsafe_pixels += static_cast<std::uint64_t>(block * block);
                ++result.held_blocks;
                continue;
            }
            if (forward.dx != 0 || forward.dy != 0) {
                motions.push_back({x, y, block, forward.dx, forward.dy});
                ++result.moved_blocks;
            }
        }
    }
    result.unsafe_ratio = static_cast<double>(unsafe_pixels) / static_cast<double>(pixel_count);
    if (result.unsafe_ratio > config_.unsafe_ratio_limit) {
        copy_frame(a, output);
        result.reason = TemporalHoldReason::UNSAFE_CONFIDENCE;
        return result;
    }

    // Resolve moving blocks only after the global confidence gate. First expose
    // B at the vacated source, then forward-warp A by half the reciprocal vector.
    // With integer nearest sampling this preserves the source palette exactly.
    std::vector<std::uint32_t> owners(static_cast<std::size_t>(pixel_count),
                                      std::numeric_limits<std::uint32_t>::max());
    for (const MotionBlock& motion : motions) {
        for (int local_y = 0; local_y < motion.size; ++local_y) {
            for (int local_x = 0; local_x < motion.size; ++local_x) {
                const int source_x = motion.x + local_x;
                const int source_y = motion.y + local_y;
                output.pixels[source_y * output.stride_pixels + source_x]
                        = b.pixels[source_y * b.stride_pixels + source_x];
            }
        }
        const int half_dx = motion.dx >= 0 ? (motion.dx + 1) / 2 : (motion.dx - 1) / 2;
        const int half_dy = motion.dy >= 0 ? (motion.dy + 1) / 2 : (motion.dy - 1) / 2;
        for (int local_y = 0; local_y < motion.size; ++local_y) {
            for (int local_x = 0; local_x < motion.size; ++local_x) {
                const int source_x = motion.x + local_x;
                const int source_y = motion.y + local_y;
                const int target_x = std::clamp(source_x + half_dx, 0, a.width - 1);
                const int target_y = std::clamp(source_y + half_dy, 0, a.height - 1);
                const auto source_index = static_cast<std::uint32_t>(
                        source_y * a.width + source_x);
                auto& owner = owners[static_cast<std::size_t>(target_y * a.width + target_x)];
                owner = std::min(owner, source_index);
            }
        }
    }
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            const auto owner = owners[static_cast<std::size_t>(y * a.width + x)];
            if (owner != std::numeric_limits<std::uint32_t>::max()) {
                output.pixels[y * output.stride_pixels + x] =
                        a.pixels[(owner / static_cast<std::uint32_t>(a.width))
                                         * a.stride_pixels
                                 + owner % static_cast<std::uint32_t>(a.width)];
            }
        }
    }

    result.generated = true;
    result.reason = TemporalHoldReason::NONE;
    return result;
}

}  // namespace flynes::video
