#include "flynes/product/control_layout.hpp"

#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace flynes::product {
namespace {

constexpr float kMinOpacity = 0.4f;
constexpr float kMaxOpacity = 1.0f;
constexpr float kMinScale = 0.5f;
constexpr float kMaxScale = 1.8f;

bool is_finite(float value)
{
    return std::isfinite(value);
}

void validate_placement(float center_x, float center_y, float scale)
{
    if (!is_finite(center_x) || !is_finite(center_y) || !is_finite(scale) || center_x < 0.0f
        || center_x > 1.0f || center_y < 0.0f || center_y > 1.0f || scale < kMinScale
        || scale > kMaxScale)
    {
        throw std::invalid_argument("invalid placement");
    }
}

std::array<ControlLayoutV2::Element, 5> all_elements()
{
    return {ControlLayoutV2::Element::DPad,
            ControlLayoutV2::Element::A,
            ControlLayoutV2::Element::B,
            ControlLayoutV2::Element::Select,
            ControlLayoutV2::Element::Start};
}

const char* element_wire_name(ControlLayoutV2::Element element)
{
    switch (element)
    {
    case ControlLayoutV2::Element::DPad:
        return "D_PAD";
    case ControlLayoutV2::Element::A:
        return "A";
    case ControlLayoutV2::Element::B:
        return "B";
    case ControlLayoutV2::Element::Select:
        return "SELECT";
    case ControlLayoutV2::Element::Start:
        return "START";
    }
    throw std::invalid_argument("unknown element");
}

ControlLayoutV2::Element element_from_wire_name(std::string_view name)
{
    if (name == "D_PAD")
    {
        return ControlLayoutV2::Element::DPad;
    }
    if (name == "A")
    {
        return ControlLayoutV2::Element::A;
    }
    if (name == "B")
    {
        return ControlLayoutV2::Element::B;
    }
    if (name == "SELECT")
    {
        return ControlLayoutV2::Element::Select;
    }
    if (name == "START")
    {
        return ControlLayoutV2::Element::Start;
    }
    throw std::invalid_argument("unknown element");
}

const char* direction_wire_name(ControlLayoutV2::Direction direction)
{
    switch (direction)
    {
    case ControlLayoutV2::Direction::Landscape:
        return "LANDSCAPE";
    }
    throw std::invalid_argument("unknown direction");
}

ControlLayoutV2::Direction direction_from_wire_name(std::string_view name)
{
    if (name == "LANDSCAPE")
    {
        return ControlLayoutV2::Direction::Landscape;
    }
    throw std::invalid_argument("unknown direction");
}

std::string format_float(float value)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(4) << static_cast<double>(value);
    return out.str();
}

float parse_float(std::string_view value)
{
    std::istringstream in{std::string(value)};
    in.imbue(std::locale::classic());
    float parsed = 0.0f;
    in >> parsed;
    if (!in || in.peek() != std::char_traits<char>::eof() || !is_finite(parsed))
    {
        throw std::invalid_argument("non-finite");
    }
    return parsed;
}

std::vector<std::string_view> split(std::string_view value, char delimiter, int limit)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= value.size())
    {
        if (limit >= 0 && static_cast<int>(parts.size()) + 1 == limit)
        {
            parts.push_back(value.substr(start));
            break;
        }

        const std::size_t next = value.find(delimiter, start);
        if (next == std::string_view::npos)
        {
            parts.push_back(value.substr(start));
            break;
        }

        parts.push_back(value.substr(start, next - start));
        start = next + 1;
    }
    return parts;
}

std::vector<std::string_view> split_fields(std::string_view value, char delimiter)
{
    return split(value, delimiter, -1);
}

} // namespace

ControlLayoutV2::Placement::Placement(float center_x_value, float center_y_value, float scale_value)
    : center_x(center_x_value), center_y(center_y_value), scale(scale_value)
{
    validate_placement(center_x, center_y, scale);
}

bool ControlLayoutV2::Placement::operator==(const Placement& other) const
{
    return center_x == other.center_x && center_y == other.center_y && scale == other.scale;
}

ControlLayoutV2::ControlLayoutV2(float opacity,
                                 Direction direction,
                                 std::map<Element, Placement> placements)
    : opacity_(opacity), direction_(direction), placements_(std::move(placements))
{
    if (!is_finite(opacity_) || opacity_ < kMinOpacity || opacity_ > kMaxOpacity
        || placements_.size() != all_elements().size())
    {
        throw std::invalid_argument("invalid layout");
    }

    for (const ControlLayoutV2::Element element : all_elements())
    {
        if (placements_.find(element) == placements_.end())
        {
            throw std::invalid_argument("missing element");
        }
    }
}

ControlLayoutV2 ControlLayoutV2::recommended()
{
    std::map<Element, Placement> placements;
    placements.emplace(Element::DPad, Placement(0.10f, 0.76f, 1.0f));
    placements.emplace(Element::A, Placement(0.94f, 0.64f, 1.0f));
    placements.emplace(Element::B, Placement(0.87f, 0.86f, 1.0f));
    placements.emplace(Element::Select, Placement(0.09f, 0.28f, 1.0f));
    placements.emplace(Element::Start, Placement(0.94f, 0.28f, 1.0f));
    return ControlLayoutV2(0.52f, Direction::Landscape, std::move(placements));
}

ControlLayoutV2::Placement ControlLayoutV2::placement(Element element) const
{
    return placements_.at(element);
}

ControlLayoutV2 ControlLayoutV2::move(Element element, float center_x, float center_y) const
{
    std::map<Element, Placement> copy = placements_;
    const Placement current = placement(element);
    copy[element] = Placement(center_x, center_y, current.scale);
    return ControlLayoutV2(opacity_, direction_, std::move(copy));
}

ControlLayoutV2 ControlLayoutV2::resize(Element element, float scale) const
{
    std::map<Element, Placement> copy = placements_;
    const Placement current = placement(element);
    copy[element] = Placement(current.center_x, current.center_y, scale);
    return ControlLayoutV2(opacity_, direction_, std::move(copy));
}

ControlLayoutV2 ControlLayoutV2::with_opacity(float value) const
{
    return ControlLayoutV2(value, direction_, placements_);
}

std::string ControlLayoutV2::encode() const
{
    std::ostringstream out;
    out << "v2|" << format_float(opacity_) << '|' << direction_wire_name(direction_);
    for (const ControlLayoutV2::Element element : all_elements())
    {
        const Placement placement_value = placement(element);
        out << '|' << element_wire_name(element) << ',' << format_float(placement_value.center_x)
            << ',' << format_float(placement_value.center_y) << ','
            << format_float(placement_value.scale);
    }
    return out.str();
}

ControlLayoutV2 ControlLayoutV2::decode(const std::string& value)
{
    if (value.empty())
    {
        throw std::invalid_argument("missing layout");
    }

    const std::vector<std::string_view> parts = split(value, '|', 8);
    if (parts.size() != 8 || parts[0] != "v2")
    {
        throw std::invalid_argument("unknown layout version");
    }

    const float opacity = parse_float(parts[1]);
    const Direction direction = direction_from_wire_name(parts[2]);
    std::map<Element, Placement> placements;

    for (std::size_t index = 3; index < parts.size(); ++index)
    {
        const std::vector<std::string_view> fields = split_fields(parts[index], ',');
        if (fields.size() != 4)
        {
            throw std::invalid_argument("invalid placement");
        }

        const Element element = element_from_wire_name(fields[0]);
        if (placements.find(element) != placements.end())
        {
            throw std::invalid_argument("duplicate placement");
        }

        placements.emplace(element,
                           Placement(parse_float(fields[1]),
                                     parse_float(fields[2]),
                                     parse_float(fields[3])));
    }

    return ControlLayoutV2(opacity, direction, std::move(placements));
}

ControlLayoutV2 ControlLayoutV2::decode_or_recommended(const std::string& value)
{
    try
    {
        return decode(value);
    }
    catch (...)
    {
        return recommended();
    }
}

bool ControlLayoutV2::operator==(const ControlLayoutV2& other) const
{
    return opacity_ == other.opacity_ && direction_ == other.direction_
           && placements_ == other.placements_;
}

} // namespace flynes::product
