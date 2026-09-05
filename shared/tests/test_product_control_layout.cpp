#include "flynes/product/control_layout.hpp"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void check_float(float left, float right, const char* message)
{
    check(std::fabs(left - right) < 1e-6f, message);
}

void test_codec_roundtrip()
{
    using flynes::product::ControlLayoutV2;

    const ControlLayoutV2 layout = ControlLayoutV2::recommended()
                                       .move(ControlLayoutV2::Element::A, 0.91f, 0.55f)
                                       .with_opacity(0.63f);
    check(layout == ControlLayoutV2::decode(layout.encode()), "roundtrip");
    check(layout.encode() == ControlLayoutV2::decode(layout.encode()).encode(), "encode stable");
}

void test_malformed_migrates_to_recommended()
{
    using flynes::product::ControlLayoutV2;

    const ControlLayoutV2 recommended = ControlLayoutV2::recommended();
    check(ControlLayoutV2::decode_or_recommended("v1|JOY") == recommended, "v1");
    check(ControlLayoutV2::decode_or_recommended("v2|NaN") == recommended, "nan");
    check(ControlLayoutV2::decode_or_recommended("") == recommended, "empty");
}

void test_recommended_placements()
{
    using flynes::product::ControlLayoutV2;

    const ControlLayoutV2 layout = ControlLayoutV2::recommended();
    check_float(layout.opacity(), 0.52f, "recommended opacity");
    check(layout.direction() == ControlLayoutV2::Direction::Landscape, "recommended landscape");

    const auto d_pad = layout.placement(ControlLayoutV2::Element::DPad);
    check_float(d_pad.center_x, 0.10f, "d-pad x");
    check_float(d_pad.center_y, 0.76f, "d-pad y");
    check_float(d_pad.scale, 1.0f, "d-pad scale");

    const auto a = layout.placement(ControlLayoutV2::Element::A);
    check_float(a.center_x, 0.94f, "a x");
    check_float(a.center_y, 0.64f, "a y");
    check_float(a.scale, 1.0f, "a scale");

    const auto b = layout.placement(ControlLayoutV2::Element::B);
    check_float(b.center_x, 0.87f, "b x");
    check_float(b.center_y, 0.86f, "b y");
    check_float(b.scale, 1.0f, "b scale");

    const auto select = layout.placement(ControlLayoutV2::Element::Select);
    check_float(select.center_x, 0.09f, "select x");
    check_float(select.center_y, 0.28f, "select y");
    check_float(select.scale, 1.0f, "select scale");

    const auto start = layout.placement(ControlLayoutV2::Element::Start);
    check_float(start.center_x, 0.94f, "start x");
    check_float(start.center_y, 0.28f, "start y");
    check_float(start.scale, 1.0f, "start scale");
}

} // namespace

int main()
{
    test_codec_roundtrip();
    test_malformed_migrates_to_recommended();
    test_recommended_placements();

    if (failures == 0)
    {
        std::puts("flynes_product_control_layout_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
