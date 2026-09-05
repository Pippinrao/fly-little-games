#include "flynes/product/gamepad_hit_map.hpp"

#include "flynes/product/nes_input_bits.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace flynes::product {
namespace {

constexpr std::array<Control, 4> kButtonControls{
    Control::A, Control::B, Control::Select, Control::Start};

const char* control_name(Control control)
{
    switch (control)
    {
    case Control::None:
        return "NONE";
    case Control::Up:
        return "UP";
    case Control::Down:
        return "DOWN";
    case Control::Left:
        return "LEFT";
    case Control::Right:
        return "RIGHT";
    case Control::B:
        return "B";
    case Control::A:
        return "A";
    case Control::Select:
        return "SELECT";
    case Control::Start:
        return "START";
    case Control::Pause:
        return "PAUSE";
    }
    return "UNKNOWN";
}

bool valid_direction_mode(DirectionControlMode mode)
{
    return mode == DirectionControlMode::Joystick
           || mode == DirectionControlMode::FixedJoystick || mode == DirectionControlMode::DPad;
}

std::string join(const std::vector<std::string>& values)
{
    std::ostringstream result;
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (index > 0)
        {
            result << "; ";
        }
        result << values[index];
    }
    return result.str();
}

} // namespace

GamepadHitMap::Bounds::Bounds(float left, float top, float right, float bottom)
    : left_(left), top_(top), right_(right), bottom_(bottom)
{
}

bool GamepadHitMap::Bounds::contains(float x, float y) const
{
    return x >= left_ && x <= right_ && y >= top_ && y <= bottom_;
}

bool GamepadHitMap::Bounds::contains(const Bounds& value) const
{
    return value.left_ >= left_ && value.top_ >= top_ && value.right_ <= right_
           && value.bottom_ <= bottom_;
}

GamepadHitMap::Bounds GamepadHitMap::Bounds::expanded(float amount) const
{
    return Bounds(left_ - amount, top_ - amount, right_ + amount, bottom_ + amount);
}

GamepadHitMap::Target::Target(Control control, Bounds bounds, Shape shape)
    : control_(control), bounds_(std::move(bounds)), shape_(shape)
{
}

GamepadHitMap::GamepadHitMap(Bounds safe_bounds,
                             Bounds dpad_bounds,
                             float density,
                             DirectionControlMode direction_mode,
                             float dead_zone,
                             std::map<Control, Target> targets)
    : safe_bounds_(std::move(safe_bounds)),
      dpad_bounds_(std::move(dpad_bounds)),
      density_(density),
      direction_mode_(direction_mode),
      dead_zone_(dead_zone),
      targets_(std::move(targets))
{
    controls_.reserve(targets_.size());
    for (const auto& entry : targets_)
    {
        controls_.push_back(entry.second);
    }
}

GamepadHitMap GamepadHitMap::standard(int width,
                                      int height,
                                      float density,
                                      int inset_left,
                                      int inset_right,
                                      int inset_top,
                                      int inset_bottom)
{
    const float safe_left = static_cast<float>(inset_left);
    const float safe_top = static_cast<float>(inset_top);
    const float safe_right = static_cast<float>(width - inset_right);
    const float safe_bottom = static_cast<float>(height - inset_bottom);
    const float dpad_size = 144.0f * density;
    const float dpad_left = safe_left + 16.0f * density;
    const float dpad_top = safe_bottom - 16.0f * density - dpad_size;
    const Bounds dpad(dpad_left, dpad_top, dpad_left + dpad_size, dpad_top + dpad_size);

    std::map<Control, Target> values;
    const float arm = 48.0f * density;
    values.emplace(Control::Up,
                   Target(Control::Up,
                          Bounds(dpad.center_x() - arm / 2.0f,
                                 dpad.top(),
                                 dpad.center_x() + arm / 2.0f,
                                 dpad.center_y()),
                          Shape::RoundedSquare));
    values.emplace(Control::Down,
                   Target(Control::Down,
                          Bounds(dpad.center_x() - arm / 2.0f,
                                 dpad.center_y(),
                                 dpad.center_x() + arm / 2.0f,
                                 dpad.bottom()),
                          Shape::RoundedSquare));
    values.emplace(Control::Left,
                   Target(Control::Left,
                          Bounds(dpad.left(),
                                 dpad.center_y() - arm / 2.0f,
                                 dpad.center_x(),
                                 dpad.center_y() + arm / 2.0f),
                          Shape::RoundedSquare));
    values.emplace(Control::Right,
                   Target(Control::Right,
                          Bounds(dpad.center_x(),
                                 dpad.center_y() - arm / 2.0f,
                                 dpad.right(),
                                 dpad.center_y() + arm / 2.0f),
                          Shape::RoundedSquare));

    const float a_size = 72.0f * density;
    const float b_size = 64.0f * density;
    const float a_center_x = safe_right - 52.0f * density;
    const float a_center_y = safe_bottom - 132.0f * density;
    const float b_center_x = a_center_x - 88.0f * density;
    const float b_center_y = safe_bottom - 48.0f * density;
    values.emplace(Control::A,
                   centered(
                       Control::A, a_center_x, a_center_y, a_size, a_size, Shape::Circle));
    values.emplace(Control::B,
                   centered(Control::B,
                            b_center_x,
                            b_center_y,
                            b_size,
                            b_size,
                            Shape::RoundedSquare));

    const float pill_width = 72.0f * density;
    const float pill_height = 48.0f * density;
    const float system_y = std::max(safe_top + pill_height / 2.0f + 8.0f * density,
                                    dpad.top() - 16.0f * density - pill_height / 2.0f);
    values.emplace(Control::Select,
                   centered(Control::Select,
                            dpad.center_x(),
                            system_y,
                            pill_width,
                            pill_height,
                            Shape::Pill));
    values.emplace(Control::Start,
                   centered(Control::Start,
                            safe_right - 43.0f * density,
                            system_y,
                            pill_width,
                            pill_height,
                            Shape::Pill));

    GamepadHitMap map(Bounds(safe_left, safe_top, safe_right, safe_bottom),
                      dpad,
                      density,
                      DirectionControlMode::DPad,
                      0.22f,
                      std::move(values));
    const std::vector<std::string> errors = map.validate();
    if (!errors.empty())
    {
        throw std::invalid_argument(join(errors));
    }
    return map;
}

GamepadHitMap GamepadHitMap::from_layout(int width,
                                         int height,
                                         float density,
                                         int inset_left,
                                         int inset_right,
                                         int inset_top,
                                         int inset_bottom,
                                         const ControlLayoutV2& layout)
{
    return from_layout(width,
                       height,
                       density,
                       inset_left,
                       inset_right,
                       inset_top,
                       inset_bottom,
                       layout,
                       DirectionControlMode::DPad,
                       0.22f);
}

GamepadHitMap GamepadHitMap::from_layout(int width,
                                         int height,
                                         float density,
                                         int inset_left,
                                         int inset_right,
                                         int inset_top,
                                         int inset_bottom,
                                         const ControlLayoutV2& layout,
                                         DirectionControlMode direction_mode,
                                         float dead_zone)
{
    if (!valid_direction_mode(direction_mode) || !std::isfinite(dead_zone) || dead_zone < 0.08f
        || dead_zone > 0.45f)
    {
        throw std::invalid_argument("invalid direction control settings");
    }

    const float safe_left = static_cast<float>(inset_left);
    const float safe_top = static_cast<float>(inset_top);
    const float safe_right = static_cast<float>(width - inset_right);
    const float safe_bottom = static_cast<float>(height - inset_bottom);
    const float safe_width = safe_right - safe_left;
    const float safe_height = safe_bottom - safe_top;
    const ControlLayoutV2::Placement dpad_placement =
        layout.placement(ControlLayoutV2::Element::DPad);
    const float dpad_size =
        (direction_mode == DirectionControlMode::DPad ? 144.0f : 128.0f) * density
        * dpad_placement.scale;
    const float dpad_center_x = clamp(safe_left + dpad_placement.center_x * safe_width,
                                      safe_left + dpad_size / 2.0f,
                                      safe_right - dpad_size / 2.0f);
    const float dpad_center_y = clamp(safe_top + dpad_placement.center_y * safe_height,
                                      safe_top + dpad_size / 2.0f,
                                      safe_bottom - dpad_size / 2.0f);
    const Bounds dpad(dpad_center_x - dpad_size / 2.0f,
                      dpad_center_y - dpad_size / 2.0f,
                      dpad_center_x + dpad_size / 2.0f,
                      dpad_center_y + dpad_size / 2.0f);

    std::map<Control, Target> values;
    const float arm = 48.0f * density * dpad_placement.scale;
    values.emplace(Control::Up,
                   Target(Control::Up,
                          Bounds(dpad.center_x() - arm / 2.0f,
                                 dpad.top(),
                                 dpad.center_x() + arm / 2.0f,
                                 dpad.center_y()),
                          Shape::RoundedSquare));
    values.emplace(Control::Down,
                   Target(Control::Down,
                          Bounds(dpad.center_x() - arm / 2.0f,
                                 dpad.center_y(),
                                 dpad.center_x() + arm / 2.0f,
                                 dpad.bottom()),
                          Shape::RoundedSquare));
    values.emplace(Control::Left,
                   Target(Control::Left,
                          Bounds(dpad.left(),
                                 dpad.center_y() - arm / 2.0f,
                                 dpad.center_x(),
                                 dpad.center_y() + arm / 2.0f),
                          Shape::RoundedSquare));
    values.emplace(Control::Right,
                   Target(Control::Right,
                          Bounds(dpad.center_x(),
                                 dpad.center_y() - arm / 2.0f,
                                 dpad.right(),
                                 dpad.center_y() + arm / 2.0f),
                          Shape::RoundedSquare));

    const auto add_layout_target = [&](Control control,
                                       ControlLayoutV2::Element element,
                                       float base_width,
                                       float base_height,
                                       Shape shape) {
        const ControlLayoutV2::Placement placement = layout.placement(element);
        const float target_width = base_width * density * placement.scale;
        const float target_height = base_height * density * placement.scale;
        const float center_x = clamp(safe_left + placement.center_x * safe_width,
                                     safe_left + target_width / 2.0f,
                                     safe_left + safe_width - target_width / 2.0f);
        const float center_y = clamp(safe_top + placement.center_y * safe_height,
                                     safe_top + target_height / 2.0f,
                                     safe_top + safe_height - target_height / 2.0f);
        values.emplace(
            control, centered(control, center_x, center_y, target_width, target_height, shape));
    };

    add_layout_target(
        Control::A, ControlLayoutV2::Element::A, 72.0f, 72.0f, Shape::Circle);
    add_layout_target(
        Control::B, ControlLayoutV2::Element::B, 64.0f, 64.0f, Shape::RoundedSquare);
    add_layout_target(
        Control::Select, ControlLayoutV2::Element::Select, 72.0f, 48.0f, Shape::Pill);
    add_layout_target(
        Control::Start, ControlLayoutV2::Element::Start, 72.0f, 48.0f, Shape::Pill);

    return GamepadHitMap(Bounds(safe_left, safe_top, safe_right, safe_bottom),
                         dpad,
                         density,
                         direction_mode,
                         dead_zone,
                         std::move(values));
}

const GamepadHitMap::Target& GamepadHitMap::target(Control control) const
{
    const auto found = targets_.find(control);
    if (found == targets_.end())
    {
        throw std::invalid_argument(control_name(control));
    }
    return found->second;
}

bool GamepadHitMap::joystick_mode() const
{
    return direction_mode_ != DirectionControlMode::DPad;
}

bool GamepadHitMap::following_joystick_mode() const
{
    return direction_mode_ == DirectionControlMode::Joystick;
}

bool GamepadHitMap::fixed_joystick_mode() const
{
    return direction_mode_ == DirectionControlMode::FixedJoystick;
}

Control GamepadHitMap::button_hit(float x, float y) const
{
    for (const Control control : kButtonControls)
    {
        if (target(control).contains(x, y))
        {
            return control;
        }
    }
    return Control::None;
}

bool GamepadHitMap::can_start_joystick(float x, float y) const
{
    if (following_joystick_mode())
    {
        return safe_bounds_.contains(x, y) && x < safe_bounds_.center_x();
    }
    if (!fixed_joystick_mode())
    {
        return false;
    }
    const float delta_x = x - dpad_bounds_.center_x();
    const float delta_y = y - dpad_bounds_.center_y();
    const float capture_radius = joystick_radius() + 16.0f * density_;
    return delta_x * delta_x + delta_y * delta_y <= capture_radius * capture_radius;
}

bool GamepadHitMap::can_start_direction(float x, float y) const
{
    if (joystick_mode())
    {
        return can_start_joystick(x, y);
    }
    return dpad_bounds_.expanded(16.0f * density_).contains(x, y);
}

float GamepadHitMap::joystick_radius() const
{
    return std::min(dpad_bounds_.width(), dpad_bounds_.height()) / 2.0f;
}

float GamepadHitMap::joystick_travel_radius() const
{
    return joystick_radius() * 0.5625f;
}

float GamepadHitMap::clamp_joystick_center_x(float x) const
{
    const float minimum = safe_bounds_.left() + joystick_radius();
    const float maximum = std::max(minimum, safe_bounds_.center_x() - joystick_radius());
    return clamp(x, minimum, maximum);
}

float GamepadHitMap::clamp_joystick_center_y(float y) const
{
    const float minimum = safe_bounds_.top() + joystick_radius();
    const float maximum = std::max(minimum, safe_bounds_.bottom() - joystick_radius());
    return clamp(y, minimum, maximum);
}

Control GamepadHitMap::hit(float x, float y) const
{
    const Control button = button_hit(x, y);
    if (button != Control::None)
    {
        return button;
    }
    if (joystick_mode())
    {
        const float delta_x = x - dpad_bounds_.center_x();
        const float delta_y = y - dpad_bounds_.center_y();
        const float capture_radius = fixed_joystick_mode()
                                         ? joystick_radius() + 16.0f * density_
                                         : joystick_radius() * 1.18f;
        if (std::hypot(delta_x, delta_y) > capture_radius)
        {
            return Control::None;
        }
    }

    const std::uint32_t direction = direction_bits(x, y, 0);
    if (direction == NES_UP)
    {
        return Control::Up;
    }
    if (direction == NES_DOWN)
    {
        return Control::Down;
    }
    if (direction == NES_LEFT)
    {
        return Control::Left;
    }
    if (direction == NES_RIGHT)
    {
        return Control::Right;
    }
    return Control::None;
}

std::uint32_t GamepadHitMap::direction_bits(float x,
                                            float y,
                                            std::uint32_t previous_bits) const
{
    if (joystick_mode())
    {
        return joystick_direction_bits(
            dpad_bounds_.center_x(), dpad_bounds_.center_y(), x, y, previous_bits);
    }

    const float release = previous_bits == 0 ? 0.0f : 8.0f * density_;
    const Bounds active = dpad_bounds_.expanded(release);
    if (!active.contains(x, y))
    {
        return 0;
    }

    const float delta_x = x - dpad_bounds_.center_x();
    const float delta_y = y - dpad_bounds_.center_y();
    const float threshold = (previous_bits == 0 ? 18.0f : 12.0f) * density_;
    std::uint32_t bits = 0;
    if (delta_x <= -threshold)
    {
        bits |= NES_LEFT;
    }
    else if (delta_x >= threshold)
    {
        bits |= NES_RIGHT;
    }
    if (delta_y <= -threshold)
    {
        bits |= NES_UP;
    }
    else if (delta_y >= threshold)
    {
        bits |= NES_DOWN;
    }
    return bits;
}

std::uint32_t GamepadHitMap::joystick_direction_bits(float center_x,
                                                     float center_y,
                                                     float x,
                                                     float y,
                                                     std::uint32_t previous_bits) const
{
    const float delta_x = x - center_x;
    const float delta_y = y - center_y;
    const float normalized = std::hypot(delta_x, delta_y) / joystick_radius();
    const float threshold =
        previous_bits == 0 ? dead_zone_ : std::max(0.08f, dead_zone_ - 0.06f);
    if (normalized < threshold)
    {
        return 0;
    }

    const float absolute_x = std::fabs(delta_x);
    const float absolute_y = std::fabs(delta_y);
    std::uint32_t bits = 0;
    if (absolute_x >= absolute_y * 0.55f)
    {
        bits |= delta_x < 0 ? NES_LEFT : NES_RIGHT;
    }
    if (absolute_y >= absolute_x * 0.55f)
    {
        bits |= delta_y < 0 ? NES_UP : NES_DOWN;
    }
    return bits;
}

float GamepadHitMap::distance_between(const Target& first, const Target& second) const
{
    const float delta_x = first.center_x() - second.center_x();
    const float delta_y = first.center_y() - second.center_y();
    const float center_distance = std::sqrt(delta_x * delta_x + delta_y * delta_y);
    return std::max(0.0f,
                    center_distance - std::max(first.width(), first.height()) / 2.0f
                        - std::max(second.width(), second.height()) / 2.0f);
}

std::vector<std::string> GamepadHitMap::validate() const
{
    std::vector<std::string> errors;
    for (const Target& value : controls_)
    {
        if (!safe_bounds_.contains(value.bounds()))
        {
            errors.emplace_back(std::string(control_name(value.control())) + " outside safe rect");
        }
    }
    if (distance_between(target(Control::A), target(Control::B)) < 24.0f * density_)
    {
        errors.emplace_back("A and B too close");
    }
    return errors;
}

GamepadHitMap::Target GamepadHitMap::centered(
    Control control, float center_x, float center_y, float width, float height, Shape shape)
{
    return Target(control,
                  Bounds(center_x - width / 2.0f,
                         center_y - height / 2.0f,
                         center_x + width / 2.0f,
                         center_y + height / 2.0f),
                  shape);
}

float GamepadHitMap::clamp(float value, float minimum, float maximum)
{
    return std::max(minimum, std::min(maximum, value));
}

} // namespace flynes::product
