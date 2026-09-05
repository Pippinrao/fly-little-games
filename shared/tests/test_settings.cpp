#include <flynes/flynes_app.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

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
                    ("flynes-settings-" + std::to_string(++sequence));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error))
            {
                break;
            }
            path_.clear();
        }
        check(!path_.empty(), "settings test creates an isolated data root");
    }

    TempRoot(const TempRoot&) = delete;
    TempRoot& operator=(const TempRoot&) = delete;

    ~TempRoot()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    std::string utf8() const { return path_.u8string(); }
    std::filesystem::path settings_path() const { return path_ / "settings.flyset01"; }

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
    check(fly_app_create(&config, &app) == FLY_RESULT_OK, "settings test creates app");
    return app;
}

fly_settings_snapshot blank_snapshot(char* locale, std::uint32_t locale_cap,
                                     char* last_played, std::uint32_t last_cap)
{
    fly_settings_snapshot value{};
    value.struct_size = FLY_SETTINGS_SNAPSHOT_V1_SIZE;
    value.version = FLY_SETTINGS_SNAPSHOT_VERSION_1;
    value.locale_tag_utf8 = locale;
    value.locale_tag_capacity = locale_cap;
    value.last_played_id_utf8 = last_played;
    value.last_played_id_capacity = last_cap;
    return value;
}

void test_frozen_enum_values()
{
    static_assert(FLY_ASPECT_FOUR_BY_THREE == 1 && FLY_ASPECT_SQUARE_PIXELS == 2 &&
                      FLY_ASPECT_INTEGER_SCALE == 3,
                  "aspect enum values changed");
    static_assert(FLY_VIDEO_QUALITY_POWER_SAVER == 1 && FLY_VIDEO_QUALITY_BALANCED == 2 &&
                      FLY_VIDEO_QUALITY_EXTREME == 3 && FLY_VIDEO_QUALITY_CUSTOM == 4,
                  "video quality enum values changed");
    static_assert(FLY_REFRESH_FOLLOW_SYSTEM == 1 &&
                      FLY_REFRESH_LEGACY_AUTO_INTEGER_MULTIPLE == 2 &&
                      FLY_REFRESH_HZ_60 == 3 && FLY_REFRESH_HZ_90 == 4 &&
                      FLY_REFRESH_HZ_120 == 5,
                  "refresh enum values changed");
    static_assert(FLY_TEMPORAL_NATIVE == 1 && FLY_TEMPORAL_MOTION_INTERPOLATION == 2,
                  "temporal enum values changed");
    static_assert(FLY_SPATIAL_NEAREST == 1 && FLY_SPATIAL_SHARP_BILINEAR == 2 &&
                      FLY_SPATIAL_MMPX == 3 && FLY_SPATIAL_SCALEFX == 4,
                  "spatial enum values changed");
    static_assert(FLY_POST_EFFECT_NONE == 1 && FLY_POST_EFFECT_CRT == 2,
                  "post-effect enum values changed");
    static_assert(FLY_LAYOUT_STANDARD_BA == 1 && FLY_LAYOUT_MIRRORED_AB == 2,
                  "layout enum values changed");
    static_assert(FLY_DIRECTION_JOYSTICK == 1 && FLY_DIRECTION_FIXED_JOYSTICK == 2 &&
                      FLY_DIRECTION_DPAD == 3,
                  "direction enum values changed");
    static_assert(FLY_HAPTIC_OFF == 1 && FLY_HAPTIC_LIGHT == 2 && FLY_HAPTIC_STANDARD == 3 &&
                      FLY_HAPTIC_STRONG == 4,
                  "haptic enum values changed");
    static_assert(FLY_AUDIO_FOCUS_PAUSE == 1 && FLY_AUDIO_FOCUS_DUCK == 2 &&
                      FLY_AUDIO_FOCUS_IGNORE == 3,
                  "audio-focus enum values changed");
    static_assert(FLY_SETTINGS_SNAPSHOT_VERSION_1 == 1u, "settings version changed");
    static_assert(offsetof(fly_settings_snapshot, struct_size) == 0u,
                  "settings prefix changed");
    static_assert(FLY_SETTINGS_SNAPSHOT_V1_SIZE == sizeof(fly_settings_snapshot),
                  "settings v1 prefix size changed");
}

void test_defaults_get_and_invalid_apply_is_atomic()
{
    TempRoot root;
    fly_app_t* app = make_app(root.utf8());
    char locale[32];
    char last_played[8];
    fly_settings_snapshot snapshot = blank_snapshot(locale, sizeof(locale), last_played,
                                                    sizeof(last_played));
    check(fly_settings_get(app, &snapshot) == FLY_RESULT_OK, "settings get succeeds");
    check(snapshot.aspect_mode == FLY_ASPECT_FOUR_BY_THREE, "default aspect is 4:3");
    check(snapshot.video_quality_preset == FLY_VIDEO_QUALITY_BALANCED,
          "default quality is balanced");
    check(snapshot.custom_refresh_policy == FLY_REFRESH_HZ_60, "default custom refresh is 60");
    check(snapshot.custom_temporal_mode == FLY_TEMPORAL_NATIVE, "default temporal is native");
    check(snapshot.custom_spatial_mode == FLY_SPATIAL_SHARP_BILINEAR,
          "default spatial is sharp bilinear");
    check(snapshot.custom_post_effect == FLY_POST_EFFECT_NONE, "default post effect is none");
    check(snapshot.adaptive_protection == 1u, "default adaptive protection is on");
    check(snapshot.layout_preset == FLY_LAYOUT_STANDARD_BA, "default layout is standard BA");
    check(snapshot.direction_mode == FLY_DIRECTION_FIXED_JOYSTICK,
          "default direction is fixed joystick");
    check(snapshot.button_scale == 1.0f, "default button scale is 1");
    check(snapshot.vertical_offset == 0.0f, "default vertical offset is 0");
    check(std::fabs(snapshot.control_opacity - 0.78f) < 0.0001f, "default opacity is 0.78");
    check(snapshot.joystick_scale == 1.0f, "default joystick scale is 1");
    check(std::fabs(snapshot.dead_zone - 0.18f) < 0.0001f, "default dead zone is 0.18");
    check(snapshot.haptic_level == FLY_HAPTIC_LIGHT, "default haptic is light");
    check(snapshot.distinct_ab_haptics == 1u && snapshot.audio_enabled == 1u &&
              snapshot.audio_focus_policy == FLY_AUDIO_FOCUS_PAUSE &&
              snapshot.autosave_enabled == 1u,
          "default audio/haptic/autosave flags");
    check(std::strcmp(locale, "system") == 0, "default locale is system");
    check(last_played[0] == '\0', "default last-played id is empty");

    fly_settings_snapshot invalid = snapshot;
    invalid.aspect_mode = 0u;
    invalid.locale_tag_utf8 = locale;
    invalid.locale_tag_utf8_length = 6u;
    invalid.last_played_id_utf8 = last_played;
    invalid.last_played_id_utf8_length = 0u;
    check(fly_settings_apply(app, &invalid) == FLY_RESULT_INVALID_ARGUMENT,
          "apply rejects an unknown aspect mode");
    fly_settings_snapshot after = blank_snapshot(locale, sizeof(locale), last_played,
                                                 sizeof(last_played));
    check(fly_settings_get(app, &after) == FLY_RESULT_OK &&
              after.aspect_mode == FLY_ASPECT_FOUR_BY_THREE,
          "invalid apply leaves prior settings unchanged");

    invalid = snapshot;
    invalid.button_scale = FLY_SETTINGS_BUTTON_SCALE_MAX + 0.01f;
    invalid.locale_tag_utf8 = locale;
    invalid.locale_tag_utf8_length = 6u;
    invalid.last_played_id_utf8 = last_played;
    invalid.last_played_id_utf8_length = 0u;
    check(fly_settings_apply(app, &invalid) == FLY_RESULT_INVALID_ARGUMENT,
          "apply rejects an out-of-range scale");
    invalid.button_scale = std::numeric_limits<float>::quiet_NaN();
    check(fly_settings_apply(app, &invalid) == FLY_RESULT_INVALID_ARGUMENT,
          "apply rejects NaN floats");
    fly_app_destroy(app);
}

void test_apply_round_trips_through_flyset01()
{
    TempRoot root;
    const std::string data_root = root.utf8();
    fly_app_t* app = make_app(data_root);
    char locale[] = "zh-CN";
    char last_played[] = "game:ABCDEF";
    fly_settings_snapshot snapshot = blank_snapshot(locale, 0u, last_played, 0u);
    snapshot.aspect_mode = FLY_ASPECT_INTEGER_SCALE;
    snapshot.video_quality_preset = FLY_VIDEO_QUALITY_CUSTOM;
    snapshot.custom_refresh_policy = FLY_REFRESH_HZ_120;
    snapshot.custom_temporal_mode = FLY_TEMPORAL_MOTION_INTERPOLATION;
    snapshot.custom_spatial_mode = FLY_SPATIAL_SCALEFX;
    snapshot.custom_post_effect = FLY_POST_EFFECT_CRT;
    snapshot.adaptive_protection = 0u;
    snapshot.layout_preset = FLY_LAYOUT_MIRRORED_AB;
    snapshot.direction_mode = FLY_DIRECTION_DPAD;
    snapshot.button_scale = 1.25f;
    snapshot.vertical_offset = -0.10f;
    snapshot.control_opacity = 0.50f;
    snapshot.joystick_scale = 0.90f;
    snapshot.dead_zone = 0.22f;
    snapshot.haptic_level = FLY_HAPTIC_STRONG;
    snapshot.distinct_ab_haptics = 0u;
    snapshot.audio_enabled = 0u;
    snapshot.audio_focus_policy = FLY_AUDIO_FOCUS_IGNORE;
    snapshot.autosave_enabled = 0u;
    snapshot.locale_tag_utf8_length = static_cast<std::uint32_t>(std::strlen(locale));
    snapshot.last_played_id_utf8_length = static_cast<std::uint32_t>(std::strlen(last_played));
    check(fly_settings_apply(app, &snapshot) == FLY_RESULT_OK, "valid apply succeeds");
    fly_app_destroy(app);

    check(std::filesystem::is_regular_file(root.settings_path()),
          "apply writes settings.flyset01 independently of the catalog");
    std::ifstream input(root.settings_path(), std::ios::binary);
    std::vector<char> magic(8);
    input.read(magic.data(), 8);
    check(input && std::memcmp(magic.data(), "FLYSET01", 8) == 0, "settings magic is FLYSET01");

    app = make_app(data_root);
    char locale_out[16];
    char last_out[32];
    fly_settings_snapshot loaded = blank_snapshot(locale_out, sizeof(locale_out), last_out,
                                                  sizeof(last_out));
    check(fly_settings_get(app, &loaded) == FLY_RESULT_OK, "reloaded settings get succeeds");
    check(loaded.aspect_mode == FLY_ASPECT_INTEGER_SCALE &&
              loaded.video_quality_preset == FLY_VIDEO_QUALITY_CUSTOM &&
              loaded.custom_refresh_policy == FLY_REFRESH_HZ_120 &&
              loaded.custom_spatial_mode == FLY_SPATIAL_SCALEFX &&
              loaded.layout_preset == FLY_LAYOUT_MIRRORED_AB &&
              loaded.direction_mode == FLY_DIRECTION_DPAD &&
              loaded.haptic_level == FLY_HAPTIC_STRONG &&
              loaded.audio_focus_policy == FLY_AUDIO_FOCUS_IGNORE,
          "enum fields round-trip through FLYSET01");
    check(std::fabs(loaded.button_scale - 1.25f) < 0.0001f &&
              std::fabs(loaded.dead_zone - 0.22f) < 0.0001f,
          "float fields round-trip");
    check(std::strcmp(locale_out, "zh-CN") == 0, "locale round-trips");
    check(std::strcmp(last_out, "game:ABCDEF") == 0, "last-played id round-trips");
    fly_app_destroy(app);
}

void test_corrupt_settings_do_not_clobber()
{
    TempRoot root;
    const std::string data_root = root.utf8();
    fly_app_t* app = make_app(data_root);
    char locale[] = "en-US";
    fly_settings_snapshot snapshot = blank_snapshot(locale, 0u, nullptr, 0u);
    snapshot.aspect_mode = FLY_ASPECT_SQUARE_PIXELS;
    snapshot.video_quality_preset = FLY_VIDEO_QUALITY_BALANCED;
    snapshot.custom_refresh_policy = FLY_REFRESH_HZ_60;
    snapshot.custom_temporal_mode = FLY_TEMPORAL_NATIVE;
    snapshot.custom_spatial_mode = FLY_SPATIAL_SHARP_BILINEAR;
    snapshot.custom_post_effect = FLY_POST_EFFECT_NONE;
    snapshot.adaptive_protection = 1u;
    snapshot.layout_preset = FLY_LAYOUT_STANDARD_BA;
    snapshot.direction_mode = FLY_DIRECTION_FIXED_JOYSTICK;
    snapshot.button_scale = 1.0f;
    snapshot.control_opacity = 0.78f;
    snapshot.joystick_scale = 1.0f;
    snapshot.dead_zone = 0.18f;
    snapshot.haptic_level = FLY_HAPTIC_LIGHT;
    snapshot.distinct_ab_haptics = 1u;
    snapshot.audio_enabled = 1u;
    snapshot.audio_focus_policy = FLY_AUDIO_FOCUS_PAUSE;
    snapshot.autosave_enabled = 1u;
    snapshot.locale_tag_utf8_length = static_cast<std::uint32_t>(std::strlen(locale));
    snapshot.last_played_id_utf8_length = 0u;
    check(fly_settings_apply(app, &snapshot) == FLY_RESULT_OK, "seed settings persist");
    fly_app_destroy(app);

    std::fstream file(root.settings_path(), std::ios::binary | std::ios::in | std::ios::out);
    check(static_cast<bool>(file), "corrupt test opens settings file");
    file.seekp(-1, std::ios::end);
    char last = 0;
    file.seekg(-1, std::ios::end);
    file.read(&last, 1);
    last = static_cast<char>(static_cast<unsigned char>(last) ^ 0xFFu);
    file.seekp(-1, std::ios::end);
    file.write(&last, 1);
    file.close();

    app = make_app(data_root);
    char locale_out[16];
    char last_out[8];
    fly_settings_snapshot loaded = blank_snapshot(locale_out, sizeof(locale_out), last_out,
                                                  sizeof(last_out));
    check(fly_settings_get(app, &loaded) == FLY_RESULT_OK &&
              loaded.aspect_mode == FLY_ASPECT_FOUR_BY_THREE,
          "corrupt FLYSET01 loads defaults");
    fly_app_destroy(app);
    check(std::filesystem::is_regular_file(root.settings_path()),
          "create does not overwrite a corrupt settings file");
}

} // namespace

int main()
{
    test_frozen_enum_values();
    test_defaults_get_and_invalid_apply_is_atomic();
    test_apply_round_trips_through_flyset01();
    test_corrupt_settings_do_not_clobber();
    if (failures == 0)
    {
        std::puts("flynes_settings_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
