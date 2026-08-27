#include "video/temporal_interpolator.h"
#include "video/temporal_scheduler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <set>
#include <vector>

namespace {
using flynes::video::MutableRgb565FrameView;
using flynes::video::Rgb565FrameView;
using flynes::video::TemporalHoldReason;
using flynes::video::TemporalInterpolator;
using flynes::video::TemporalScheduler;
using flynes::video::TemporalSlot;

int failures = 0;

void check(bool condition, const char* message) {
    std::printf("  [%s] %s\n", condition ? "ok  " : "FAIL", message);
    if (!condition) ++failures;
}

struct Frame {
    int width;
    int height;
    std::vector<std::uint16_t> pixels;

    Frame(int w, int h, std::uint16_t fill = 0) : width(w), height(h), pixels(w * h, fill) {}
    Rgb565FrameView view() const { return {pixels.data(), width, height, width}; }
    MutableRgb565FrameView mutable_view() { return {pixels.data(), width, height, width}; }
    std::uint16_t& at(int x, int y) { return pixels[static_cast<std::size_t>(y * width + x)]; }
    std::uint16_t at(int x, int y) const { return pixels[static_cast<std::size_t>(y * width + x)]; }
};

void fill_rect(Frame& frame, int left, int top, int right, int bottom, std::uint16_t color) {
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x) frame.at(x, y) = color;
}

void test_two_pixel_motion_and_palette_union() {
    constexpr std::uint16_t kBlack = 0x0000;
    constexpr std::uint16_t kRed = 0xf800;
    constexpr std::uint16_t kHud = 0xffff;
    Frame a(32, 16, kBlack), b(32, 16, kBlack), midpoint(32, 16, 0x07e0);
    fill_rect(a, 4, 8, 8, 12, kRed);
    fill_rect(b, 6, 8, 10, 12, kRed);
    fill_rect(a, 0, 0, 32, 4, kHud);
    fill_rect(b, 0, 0, 32, 4, kHud);

    TemporalInterpolator interpolator;
    const auto result = interpolator.interpolate_midpoint(a.view(), b.view(), midpoint.mutable_view());
    check(result.generated, "two-pixel motion produces a synthesized midpoint");
    bool shifted = true;
    for (int y = 8; y < 12; ++y) {
        for (int x = 0; x < 32; ++x) {
            const auto expected = (x >= 5 && x < 9) ? kRed : kBlack;
            shifted = shifted && midpoint.at(x, y) == expected;
        }
    }
    check(shifted, "two-pixel sprite motion lands exactly one pixel forward");
    check(std::equal(a.pixels.begin(), a.pixels.begin() + 32 * 4, midpoint.pixels.begin()),
          "stationary HUD is locked to the real frame");

    std::set<std::uint16_t> source_palette(a.pixels.begin(), a.pixels.end());
    source_palette.insert(b.pixels.begin(), b.pixels.end());
    const bool palette_safe = std::all_of(midpoint.pixels.begin(), midpoint.pixels.end(),
        [&](std::uint16_t color) { return source_palette.count(color) != 0; });
    check(palette_safe, "every synthesized pixel belongs to the A/B RGB565 palette union");
}

void test_occlusion_holds_prior_pixels() {
    Frame a(32, 16, 0x001f), b = a, midpoint(32, 16);
    fill_rect(b, 24, 8, 28, 12, 0xffe0);
    TemporalInterpolator interpolator;
    const auto result = interpolator.interpolate_midpoint(a.view(), b.view(), midpoint.mutable_view());
    check(result.generated, "a small occlusion does not disable synthesis for the whole frame");
    bool held = true;
    for (int y = 8; y < 12; ++y)
        for (int x = 24; x < 28; ++x) held = held && midpoint.at(x, y) == a.at(x, y);
    check(held, "low-confidence occlusion slots hold the prior real frame");
}

void test_scene_cut_and_full_flash_hold() {
    Frame black(32, 16, 0x0000), white(32, 16, 0xffff), output(32, 16);
    TemporalInterpolator interpolator;
    const auto flash = interpolator.interpolate_midpoint(
            black.view(), white.view(), output.mutable_view());
    check(!flash.generated && flash.reason == TemporalHoldReason::FULL_FLASH,
          "full-screen flash holds A instead of manufacturing a gray frame");
    check(output.pixels == black.pixels, "full-screen flash output is the prior real frame");

    Frame random_a(32, 16), random_b(32, 16);
    std::mt19937 random(0x46594e45u);
    for (auto& pixel : random_a.pixels) pixel = static_cast<std::uint16_t>(random());
    for (auto& pixel : random_b.pixels) pixel = static_cast<std::uint16_t>(random());
    const auto cut = interpolator.interpolate_midpoint(
            random_a.view(), random_b.view(), output.mutable_view());
    check(!cut.generated && cut.reason == TemporalHoldReason::SCENE_CUT,
          "scene cut holds the prior real frame");
}

void test_unsafe_confidence_ratio_holds_frame() {
    Frame a(40, 20, 0x07e0), b = a, output(40, 20);
    std::mt19937 random(7u);
    for (int y = 4; y < 16; ++y)
        for (int x = 4; x < 20; ++x) b.at(x, y) = static_cast<std::uint16_t>(random());
    TemporalInterpolator interpolator;
    const auto result = interpolator.interpolate_midpoint(a.view(), b.view(), output.mutable_view());
    check(!result.generated && result.reason == TemporalHoldReason::UNSAFE_CONFIDENCE,
          "more than ten percent unsafe slots fail closed");
    check(result.unsafe_ratio > 0.10, "unsafe ratio is reported for qualification evidence");
    check(output.pixels == a.pixels, "unsafe frame returns the prior real frame unchanged");
}

void test_converging_motion_uses_deterministic_lowest_source_owner() {
    Frame a(32, 16, 0x0000), b(32, 16, 0x0000), output(32, 16);
    fill_rect(a, 4, 8, 8, 12, 0xf800);
    fill_rect(a, 10, 8, 14, 12, 0x001f);
    fill_rect(b, 6, 8, 10, 12, 0xf800);
    fill_rect(b, 10, 8, 12, 12, 0x001f);
    TemporalInterpolator interpolator;
    const auto result = interpolator.interpolate_midpoint(a.view(), b.view(),
                                                           output.mutable_view());
    check(result.generated, "converging motion remains a valid synthesized fixture");
    const Frame first = output;
    for (int i = 0; i < 32; ++i) {
        const auto again = interpolator.interpolate_midpoint(a.view(), b.view(),
                                                             output.mutable_view());
        check(again.generated && output.pixels == first.pixels,
              "overlapping warp ownership is deterministic across runs");
    }
}

void test_scheduler(double display_hz, double expected_adjustment_seconds) {
    TemporalScheduler scheduler(60.0988, display_hz);
    constexpr double kDurationSeconds = 600.0;
    const int ticks = static_cast<int>(std::floor(display_hz * kDurationSeconds));
    std::uint64_t adjustments = 0;
    bool phase_bounded = true;
    for (int i = 0; i < ticks; ++i) {
        const TemporalSlot slot = scheduler.advance();
        phase_bounded = phase_bounded && scheduler.phase() >= 0.0 && scheduler.phase() < 1.0;
        if (slot.adjusts_nominal_alternation) ++adjustments;
    }
    check(phase_bounded, "scheduler phase remains bounded over ten minutes");
    const double observed = adjustments == 0 ? 0.0 : kDurationSeconds / adjustments;
    check(adjustments > 0, "fractional cadence produces explicit adjustment slots");
    check(std::abs(observed - expected_adjustment_seconds) < 0.06,
          "cadence adjustment average matches the source/display phase ratio");
    check(scheduler.real_slots() + scheduler.interpolated_slots()
                    == static_cast<std::uint64_t>(ticks),
          "every presentation timestamp receives exactly one classified slot");
}

void test_timestamp_driven_scheduler() {
    TemporalScheduler scheduler(60.0988, 120.0);
    const std::int64_t interval = 8'333'333;
    for (int i = 0; i < 1200; ++i)
        scheduler.advance_at(1'000'000'000LL + static_cast<std::int64_t>(i) * interval);
    check(scheduler.timestamp_discontinuities() == 0,
          "monotonic 120Hz presentation timestamps keep cadence continuous");
    check(scheduler.real_slots() > scheduler.interpolated_slots(),
          "actual timestamp deltas preserve fractional source cadence");
    scheduler.advance_at(500);
    check(scheduler.timestamp_discontinuities() == 1,
          "backward presentation time is detected and resets phase safely");
}

void test_source_frame_cadence_is_bounded() {
    TemporalScheduler scheduler(60.0988, 120.0);
    constexpr int kSourceFrames = 60'099;
    std::uint64_t slots = 0;
    std::uint64_t adjustments = 0;
    bool bounded = true;
    for (int i = 0; i < kSourceFrames; ++i) {
        const auto cadence = scheduler.advance_source_frame();
        bounded = bounded && (cadence.presentation_slots == 1
                || cadence.presentation_slots == 2);
        slots += cadence.presentation_slots;
        if (cadence.adjusts_nominal_alternation) ++adjustments;
    }
    check(bounded, "each source frame owns one real slot and at most one midpoint");
    const double expected = static_cast<double>(kSourceFrames) * 120.0 / 60.0988;
    check(std::abs(static_cast<double>(slots) - expected) <= 1.0,
          "source-driven cadence neither overfeeds nor starves 120Hz");
    const double seconds_per_adjustment = 1000.0 / static_cast<double>(adjustments);
    check(std::abs(seconds_per_adjustment - 5.06) < 0.06,
          "midpoint omission averages about once per 5.06 seconds");
    check(adjustments == scheduler.cadence_adjustments(),
          "every omitted midpoint is exported as evidence");
}
}  // namespace

int main() {
    std::puts("=== FlyNES temporal interpolation host contract ===");
    test_two_pixel_motion_and_palette_union();
    test_occlusion_holds_prior_pixels();
    test_scene_cut_and_full_flash_hold();
    test_unsafe_confidence_ratio_holds_frame();
    test_converging_motion_uses_deterministic_lowest_source_owner();
    test_scheduler(120.0, 5.06);
    test_scheduler(119.88, 3.15);
    test_timestamp_driven_scheduler();
    test_source_frame_cadence_is_bounded();
    std::printf("=== RESULT: %s (%d failures) ===\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
