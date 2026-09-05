#pragma once

#include <array>
#include <map>
#include <string>

namespace flynes::product {

class ControlLayoutV2 final
{
public:
    enum class Element
    {
        DPad,
        A,
        B,
        Select,
        Start,
    };

    enum class Direction
    {
        Landscape,
    };

    struct Placement final
    {
        float center_x = 0.0f;
        float center_y = 0.0f;
        float scale = 1.0f;

        Placement() = default;
        Placement(float center_x_value, float center_y_value, float scale_value);

        bool operator==(const Placement& other) const;
        bool operator!=(const Placement& other) const { return !(*this == other); }
    };

    static ControlLayoutV2 recommended();

    float opacity() const { return opacity_; }
    Direction direction() const { return direction_; }
    Placement placement(Element element) const;

    ControlLayoutV2 move(Element element, float center_x, float center_y) const;
    ControlLayoutV2 resize(Element element, float scale) const;
    ControlLayoutV2 with_opacity(float value) const;

    std::string encode() const;
    static ControlLayoutV2 decode(const std::string& value);
    static ControlLayoutV2 decode_or_recommended(const std::string& value);

    bool operator==(const ControlLayoutV2& other) const;
    bool operator!=(const ControlLayoutV2& other) const { return !(*this == other); }

private:
    ControlLayoutV2(float opacity, Direction direction, std::map<Element, Placement> placements);

    float opacity_ = 0.52f;
    Direction direction_ = Direction::Landscape;
    std::map<Element, Placement> placements_;
};

} // namespace flynes::product
