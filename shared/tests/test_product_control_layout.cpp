#include "flynes/product/control_layout.hpp"

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

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

class LocaleGuard
{
public:
    explicit LocaleGuard(const char* locale_name)
    {
        if (const char* current = std::setlocale(LC_ALL, nullptr))
        {
            saved_ = current;
        }
        std::setlocale(LC_ALL, locale_name);
    }

    ~LocaleGuard()
    {
        if (!saved_.empty())
        {
            std::setlocale(LC_ALL, saved_.c_str());
        }
    }

private:
    std::string saved_;
};

const char* comma_decimal_locale()
{
#if defined(_WIN32)
    return "French_France.1252";
#else
    return "fr_FR.UTF-8";
#endif
}

void test_encode_decode_uses_us_decimal_under_comma_locale()
{
    using flynes::product::ControlLayoutV2;

    LocaleGuard locale_guard(comma_decimal_locale());

    const ControlLayoutV2 layout = ControlLayoutV2::recommended()
                                       .move(ControlLayoutV2::Element::A, 0.91f, 0.55f)
                                       .with_opacity(0.63f);
    const std::string encoded = layout.encode();

    check(encoded.find("0.6300") != std::string::npos, "opacity uses dot decimal");
    check(encoded.find("0,6300") == std::string::npos, "opacity avoids comma decimal");
    check(layout == ControlLayoutV2::decode(encoded), "roundtrip under comma locale");
}

void test_recommended_wire_format()
{
    using flynes::product::ControlLayoutV2;

    static constexpr const char* kExpected =
        "v2|0.5200|LANDSCAPE|"
        "D_PAD,0.1000,0.7600,1.0000|"
        "A,0.9400,0.6400,1.0000|"
        "B,0.8700,0.8600,1.0000|"
        "SELECT,0.0900,0.2800,1.0000|"
        "START,0.9400,0.2800,1.0000";

    check(ControlLayoutV2::recommended().encode() == kExpected, "recommended wire format");
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
    test_encode_decode_uses_us_decimal_under_comma_locale();
    test_recommended_wire_format();
    test_recommended_placements();

    if (failures == 0)
    {
        std::puts("flynes_product_control_layout_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
