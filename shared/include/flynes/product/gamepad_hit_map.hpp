#pragma once

#include "flynes/product/control_layout.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace flynes::product {

enum class Control
{
    None,
    Up,
    Down,
    Left,
    Right,
    B,
    A,
    Select,
    Start,
    Pause,
};

enum class DirectionControlMode
{
    Joystick,
    FixedJoystick,
    DPad,
};

class GamepadHitMap final
{
public:
    enum class Shape
    {
        Circle,
        RoundedSquare,
        Pill,
    };

    class Bounds final
    {
    public:
        Bounds(float left, float top, float right, float bottom);

        float left() const { return left_; }
        float top() const { return top_; }
        float right() const { return right_; }
        float bottom() const { return bottom_; }
        float center_x() const { return (left_ + right_) / 2.0f; }
        float center_y() const { return (top_ + bottom_) / 2.0f; }
        float width() const { return right_ - left_; }
        float height() const { return bottom_ - top_; }
        bool contains(float x, float y) const;
        bool contains(const Bounds& value) const;
        Bounds expanded(float amount) const;

    private:
        float left_;
        float top_;
        float right_;
        float bottom_;
    };

    class Target final
    {
    public:
        Target(Control control, Bounds bounds, Shape shape);

        Control control() const { return control_; }
        Shape shape() const { return shape_; }
        const Bounds& bounds() const { return bounds_; }
        float left() const { return bounds_.left(); }
        float top() const { return bounds_.top(); }
        float right() const { return bounds_.right(); }
        float bottom() const { return bounds_.bottom(); }
        float center_x() const { return bounds_.center_x(); }
        float center_y() const { return bounds_.center_y(); }
        float width() const { return bounds_.width(); }
        float height() const { return bounds_.height(); }
        bool contains(float x, float y) const { return bounds_.contains(x, y); }

    private:
        Control control_;
        Bounds bounds_;
        Shape shape_;
    };

    static GamepadHitMap standard(int width,
                                  int height,
                                  float density,
                                  int inset_left,
                                  int inset_right,
                                  int inset_top,
                                  int inset_bottom);

    static GamepadHitMap from_layout(int width,
                                     int height,
                                     float density,
                                     int inset_left,
                                     int inset_right,
                                     int inset_top,
                                     int inset_bottom,
                                     const ControlLayoutV2& layout);

    static GamepadHitMap from_layout(int width,
                                     int height,
                                     float density,
                                     int inset_left,
                                     int inset_right,
                                     int inset_top,
                                     int inset_bottom,
                                     const ControlLayoutV2& layout,
                                     DirectionControlMode direction_mode,
                                     float dead_zone);

    const Target& target(Control control) const;
    const std::vector<Target>& controls() const { return controls_; }
    const Bounds& dpad_bounds() const { return dpad_bounds_; }
    DirectionControlMode direction_mode() const { return direction_mode_; }
    float dead_zone() const { return dead_zone_; }

    bool joystick_mode() const;
    bool following_joystick_mode() const;
    bool fixed_joystick_mode() const;
    Control button_hit(float x, float y) const;
    bool can_start_joystick(float x, float y) const;
    bool can_start_direction(float x, float y) const;
    float joystick_radius() const;
    float joystick_travel_radius() const;
    float clamp_joystick_center_x(float x) const;
    float clamp_joystick_center_y(float y) const;
    Control hit(float x, float y) const;
    std::uint32_t direction_bits(float x, float y, std::uint32_t previous_bits) const;
    std::uint32_t joystick_direction_bits(float center_x,
                                          float center_y,
                                          float x,
                                          float y,
                                          std::uint32_t previous_bits) const;
    float distance_between(const Target& first, const Target& second) const;
    std::vector<std::string> validate() const;

private:
    GamepadHitMap(Bounds safe_bounds,
                  Bounds dpad_bounds,
                  float density,
                  DirectionControlMode direction_mode,
                  float dead_zone,
                  std::map<Control, Target> targets);

    static Target centered(
        Control control, float center_x, float center_y, float width, float height, Shape shape);
    static float clamp(float value, float minimum, float maximum);

    Bounds safe_bounds_;
    Bounds dpad_bounds_;
    float density_;
    DirectionControlMode direction_mode_;
    float dead_zone_;
    std::map<Control, Target> targets_;
    std::vector<Target> controls_;
};

} // namespace flynes::product
