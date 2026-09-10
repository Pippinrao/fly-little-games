#include <flynes/flynes_app.h>
#include <flynes/product/control_layout.hpp>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
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

class TempRoot final
{
public:
    TempRoot()
    {
        static unsigned int sequence = 0u;
        for (unsigned int attempt = 0u; attempt < 100u; ++attempt)
        {
            path_ = std::filesystem::temp_directory_path() /
                    ("flynes-control-layout-" + std::to_string(++sequence));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error))
            {
                break;
            }
            path_.clear();
        }
        check(!path_.empty(), "control layout test creates an isolated data root");
    }

    TempRoot(const TempRoot&) = delete;
    TempRoot& operator=(const TempRoot&) = delete;

    ~TempRoot()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    std::string utf8() const { return path_.u8string(); }
    std::filesystem::path layout_path() const { return path_ / "control_layout.v2"; }

private:
    std::filesystem::path path_;
};

const fly_platform_capabilities* capabilities()
{
    static fly_platform_capabilities value{};
    value.struct_size = FLY_PLATFORM_CAPABILITIES_V1_SIZE;
    value.version = FLY_PLATFORM_CAPABILITIES_VERSION_1;
    return &value;
}

fly_app_t* make_app(const std::string& data_root)
{
    fly_app_config config{};
    config.struct_size = FLY_APP_CONFIG_V1_SIZE;
    config.version = FLY_APP_CONFIG_VERSION_1;
    config.data_root_utf8 = data_root.data();
    config.cache_root_utf8 = data_root.data();
    config.platform_capabilities = capabilities();
    config.data_root_utf8_length = static_cast<std::uint32_t>(data_root.size());
    config.cache_root_utf8_length = static_cast<std::uint32_t>(data_root.size());
    fly_app_t* app = nullptr;
    check(fly_app_create(&config, &app) == FLY_RESULT_OK, "control layout test creates app");
    return app;
}

void test_get_empty_returns_recommended()
{
    using flynes::product::ControlLayoutV2;

    TempRoot root;
    fly_app_t* app = make_app(root.utf8());
    const std::string expected = ControlLayoutV2::recommended().encode();

    std::uint32_t required = 0u;
    check(fly_control_layout_get(app, nullptr, 0u, &required) == FLY_RESULT_BUFFER_TOO_SMALL,
          "empty get reports required size");
    check(required == static_cast<std::uint32_t>(expected.size() + 1u),
          "empty get required matches recommended encode plus NUL");

    std::string buffer(expected.size() + 1u, '\0');
    check(fly_control_layout_get(app,
                                 buffer.data(),
                                 static_cast<std::uint32_t>(buffer.size()),
                                 &required) == FLY_RESULT_OK,
          "empty get succeeds with adequate buffer");
    check(required == static_cast<std::uint32_t>(expected.size() + 1u),
          "empty get required unchanged on success");
    check(std::strcmp(buffer.c_str(), expected.c_str()) == 0,
          "empty get returns recommended encode");

    fly_app_destroy(app);
}

void test_apply_moved_a_roundtrip()
{
    using flynes::product::ControlLayoutV2;

    TempRoot root;
    fly_app_t* app = make_app(root.utf8());
    const std::string encoded = ControlLayoutV2::recommended()
                                  .move(ControlLayoutV2::Element::A, 0.91f, 0.55f)
                                  .encode();

    check(fly_control_layout_apply(app,
                                   encoded.c_str(),
                                   static_cast<std::uint32_t>(encoded.size())) == FLY_RESULT_OK,
          "apply moved-A succeeds");
    check(std::filesystem::is_regular_file(root.layout_path()),
          "apply writes control_layout.v2 beside settings");

    std::string buffer(encoded.size() + 64u, '\0');
    std::uint32_t required = 0u;
    check(fly_control_layout_get(app,
                                 buffer.data(),
                                 static_cast<std::uint32_t>(buffer.size()),
                                 &required) == FLY_RESULT_OK,
          "get after apply succeeds");
    buffer.resize(required > 0u ? required - 1u : 0u);
    check(buffer == encoded, "get after apply returns moved-A encode");

    fly_app_destroy(app);

    app = make_app(root.utf8());
    buffer.assign(encoded.size() + 64u, '\0');
    check(fly_control_layout_get(app,
                                 buffer.data(),
                                 static_cast<std::uint32_t>(buffer.size()),
                                 &required) == FLY_RESULT_OK,
          "reload after apply succeeds");
    buffer.resize(required > 0u ? required - 1u : 0u);
    check(buffer == encoded, "reload after apply returns moved-A encode");
    fly_app_destroy(app);
}

void test_apply_invalid_persists_recommended()
{
    using flynes::product::ControlLayoutV2;

    TempRoot root;
    fly_app_t* app = make_app(root.utf8());
    const std::string expected = ControlLayoutV2::recommended().encode();
    static constexpr const char kInvalid[] = "v1|JOY";

    check(fly_control_layout_apply(app,
                                   kInvalid,
                                   static_cast<std::uint32_t>(std::strlen(kInvalid))) ==
              FLY_RESULT_OK,
          "apply invalid payload succeeds with recommended fallback");

    std::string buffer(expected.size() + 64u, '\0');
    std::uint32_t required = 0u;
    check(fly_control_layout_get(app,
                                 buffer.data(),
                                 static_cast<std::uint32_t>(buffer.size()),
                                 &required) == FLY_RESULT_OK,
          "get after invalid apply succeeds");
    buffer.resize(required > 0u ? required - 1u : 0u);
    check(buffer == expected, "get after invalid apply returns recommended encode");
    check(std::filesystem::is_regular_file(root.layout_path()),
          "invalid apply persists control_layout.v2");

    fly_app_destroy(app);
}

} // namespace

int main()
{
    test_get_empty_returns_recommended();
    test_apply_moved_a_roundtrip();
    test_apply_invalid_persists_recommended();

    if (failures == 0)
    {
        std::puts("flynes_control_layout_persist_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
