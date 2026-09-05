#include "flynes/product/gamepad_hit_map.hpp"
#include "flynes/product/nes_input_bits.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {

using flynes::product::Control;
using flynes::product::ControlLayoutV2;
using flynes::product::DirectionControlMode;
using flynes::product::GamepadHitMap;
using namespace flynes::product;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void test_standard_layout_uses_classic_dpad_and_ergonomic_action_sizes()
{
    constexpr float density = 2.75f;
    const auto map = GamepadHitMap::standard(2340, 1080, density, 0, 132, 0, 0);

    check(std::fabs(map.dpad_bounds().width() - 144.0f * density) < 0.1f, "dpad width");
    check(std::fabs(map.dpad_bounds().height() - 144.0f * density) < 0.1f, "dpad height");
    check(std::fabs(map.target(Control::A).width() - 72.0f * density) < 0.1f, "A width");
    check(std::fabs(map.target(Control::B).width() - 64.0f * density) < 0.1f, "B width");
    check(map.controls().size() == 8, "standard excludes NONE and PAUSE");
    check(map.validate().empty(), "standard validates");
}

void test_a_and_b_are_diagonal_with_at_least_twenty_four_dp_gap()
{
    constexpr float density = 2.75f;
    const auto map = GamepadHitMap::standard(2340, 1080, density, 0, 132, 0, 0);
    const auto& a = map.target(Control::A);
    const auto& b = map.target(Control::B);

    check(a.center_x() > b.center_x(), "A right of B");
    check(a.center_y() < b.center_y(), "A above B");
    check(map.distance_between(a, b) >= 24.0f * density, "A/B gap");
}

void test_select_and_start_stay_outside_central_seventy_percent()
{
    constexpr int width = 2340;
    constexpr float density = 2.75f;
    const auto map = GamepadHitMap::standard(width, 1080, density, 0, 132, 0, 0);

    check(map.target(Control::Select).right() <= width * 0.15f, "select left");
    check(map.target(Control::Start).left() >= width * 0.85f, "start right");
    check(map.target(Control::Select).width() >= 72.0f * density, "select width");
    check(map.target(Control::Start).height() >= 48.0f * density, "start height");
}

void test_dpad_directions_and_diagonal_are_resolved_without_opposites()
{
    constexpr float density = 2.75f;
    const auto map = GamepadHitMap::standard(2340, 1080, density, 0, 132, 0, 0);
    const auto dpad = map.dpad_bounds();

    check(map.direction_bits(dpad.center_x(), dpad.top() + 8.0f, 0) == NES_UP, "up");
    check(map.direction_bits(dpad.center_x(), dpad.bottom() - 8.0f, 0) == NES_DOWN, "down");
    check(map.direction_bits(dpad.left() + 8.0f, dpad.center_y(), 0) == NES_LEFT, "left");
    check(map.direction_bits(dpad.right() - 8.0f, dpad.center_y(), 0) == NES_RIGHT, "right");
    check(map.direction_bits(dpad.left() + 12.0f, dpad.top() + 12.0f, 0)
              == (NES_UP | NES_LEFT),
          "diagonal");
}

void test_dpad_hysteresis_retains_direction_near_threshold_then_cancels_on_exit()
{
    constexpr float density = 2.75f;
    const auto map = GamepadHitMap::standard(2340, 1080, density, 0, 132, 0, 0);
    const auto dpad = map.dpad_bounds();

    check(map.direction_bits(
              dpad.center_x() + 20.0f * density, dpad.center_y(), 0)
              == NES_RIGHT,
          "right engages");
    check(map.direction_bits(
              dpad.center_x() + 13.0f * density, dpad.center_y(), NES_RIGHT)
              == NES_RIGHT,
          "right hysteresis");
    check(map.direction_bits(
              dpad.right() + 13.0f * density, dpad.center_y(), NES_RIGHT)
              == 0,
          "right cancels outside");
}

void test_from_layout_rejects_every_non_finite_dead_zone()
{
    const float values[] = {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity()};
    for (const float dead_zone : values)
    {
        bool threw = false;
        try
        {
            static_cast<void>(GamepadHitMap::from_layout(
                2340,
                1080,
                2.75f,
                0,
                132,
                0,
                0,
                ControlLayoutV2::recommended(),
                DirectionControlMode::FixedJoystick,
                dead_zone));
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        check(threw, "non-finite dead zone rejected");
    }
}

void test_hit_map_applies_normalized_positions_and_scale()
{
    const auto layout = ControlLayoutV2::recommended()
                            .move(ControlLayoutV2::Element::A, 0.75f, 0.40f)
                            .resize(ControlLayoutV2::Element::A, 1.2f);
    const auto map = GamepadHitMap::from_layout(2000, 1000, 2.0f, 100, 100, 20, 20, layout);

    check(std::fabs(map.target(Control::A).center_x() - (100.0f + 0.75f * 1800.0f)) < 0.1f,
          "layout A center x");
    check(std::fabs(map.target(Control::A).center_y() - (20.0f + 0.40f * 960.0f)) < 0.1f,
          "layout A center y");
    check(std::fabs(map.target(Control::A).width() - (72.0f * 2.0f * 1.2f)) < 0.1f,
          "layout A width");
}

void test_recommended_layout_remains_inside_all_safe_matrices()
{
    const int matrices[][4] = {
        {1920, 1080, 0, 0}, {2340, 1080, 0, 132}, {1280, 720, 40, 40}, {1433, 1080, 0, 0}};
    for (const auto& matrix : matrices)
    {
        const auto map = GamepadHitMap::from_layout(matrix[0],
                                                    matrix[1],
                                                    2.0f,
                                                    matrix[2],
                                                    matrix[3],
                                                    0,
                                                    0,
                                                    ControlLayoutV2::recommended());
        for (const auto& target : map.controls())
        {
            check(target.left() >= matrix[2] - 0.1f, "matrix target left");
            check(target.right() <= matrix[0] - matrix[3] + 0.1f, "matrix target right");
            check(target.top() >= -0.1f, "matrix target top");
            check(target.bottom() <= matrix[1] + 0.1f, "matrix target bottom");
        }
    }
}

static_assert(NES_A == 0x01);
static_assert(NES_B == 0x02);
static_assert(NES_SELECT == 0x04);
static_assert(NES_START == 0x08);
static_assert(NES_UP == 0x10);
static_assert(NES_DOWN == 0x20);
static_assert(NES_LEFT == 0x40);
static_assert(NES_RIGHT == 0x80);

} // namespace

int main()
{
    test_standard_layout_uses_classic_dpad_and_ergonomic_action_sizes();
    test_a_and_b_are_diagonal_with_at_least_twenty_four_dp_gap();
    test_select_and_start_stay_outside_central_seventy_percent();
    test_dpad_directions_and_diagonal_are_resolved_without_opposites();
    test_dpad_hysteresis_retains_direction_near_threshold_then_cancels_on_exit();
    test_from_layout_rejects_every_non_finite_dead_zone();
    test_hit_map_applies_normalized_positions_and_scale();
    test_recommended_layout_remains_inside_all_safe_matrices();

    if (failures == 0)
    {
        std::puts("flynes_product_gamepad_hit_map_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
