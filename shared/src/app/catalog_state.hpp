#ifndef FLYNES_APP_CATALOG_STATE_HPP
#define FLYNES_APP_CATALOG_STATE_HPP

#include <flynes/flynes_app.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace flynes::app {

struct SourceKey final
{
    std::array<std::uint8_t, 16> uuid{};
    std::uint32_t scope = 0u;
};

inline bool same_source(const SourceKey& left, const SourceKey& right) noexcept
{
    return left.scope == right.scope && left.uuid == right.uuid;
}

struct SourceRecord final
{
    SourceKey key;
    std::uint32_t last_completeness = FLY_SCAN_COMPLETENESS_FULL;
};

struct UserRecord final
{
    std::string canonical_id;
    bool favorite = false;
    std::uint64_t favorite_revision = 0u;
    std::uint64_t last_played_sequence = 0u;
    std::uint32_t play_count = 0u;
};

struct CatalogEntryData final
{
    SourceKey source;
    std::uint64_t payload_size = 0u;
    std::uint64_t physical_size = 0u;
    std::uint64_t expected_bytes = 0u;
    std::uint64_t prg_bytes = 0u;
    std::uint64_t chr_bytes = 0u;
    std::int32_t mapper = -1;
    std::int32_t submapper = -1;
    std::uint32_t disk_sides = 0u;
    std::array<std::uint8_t, 20> payload_sha1{};
    std::array<std::uint8_t, 32> payload_sha256{};
    std::array<std::uint8_t, 32> physical_sha256{};
    std::array<std::uint8_t, 4> payload_crc32{};
    std::uint32_t package_format = 0u;
    std::uint32_t rom_format = 0u;
    std::uint32_t compatibility_state = 0u;
    std::uint32_t compatibility_reason = 0u;
    std::uint32_t freshness = FLY_CATALOG_FRESHNESS_FRESH;
    std::uint32_t flags = 0u;
    std::string canonical_id;
    std::string variant_id;
    std::string display_name;
    std::string source_relative_path;
    std::string package_id;
    std::vector<std::uint8_t> zip_raw_name;
    std::int32_t zip_local_header_offset = -1;
};

struct CatalogData final
{
    std::uint64_t generation = 0u;
    std::vector<CatalogEntryData> entries;
    std::vector<SourceRecord> sources;
    std::vector<UserRecord> users;
    std::uint64_t next_favorite_revision = 0u;
    std::uint64_t next_play_sequence = 0u;
};

struct SettingsData final
{
    std::uint32_t aspect_mode = FLY_ASPECT_FOUR_BY_THREE;
    std::uint32_t video_quality_preset = FLY_VIDEO_QUALITY_BALANCED;
    std::uint32_t custom_refresh_policy = FLY_REFRESH_HZ_60;
    std::uint32_t custom_temporal_mode = FLY_TEMPORAL_NATIVE;
    std::uint32_t custom_spatial_mode = FLY_SPATIAL_SHARP_BILINEAR;
    std::uint32_t custom_post_effect = FLY_POST_EFFECT_NONE;
    std::uint32_t adaptive_protection = 1u;
    std::uint32_t layout_preset = FLY_LAYOUT_STANDARD_BA;
    std::uint32_t direction_mode = FLY_DIRECTION_FIXED_JOYSTICK;
    float button_scale = 1.0f;
    float vertical_offset = 0.0f;
    float control_opacity = 0.78f;
    float joystick_scale = 1.0f;
    float dead_zone = 0.18f;
    std::uint32_t haptic_level = FLY_HAPTIC_LIGHT;
    std::uint32_t distinct_ab_haptics = 1u;
    std::uint32_t audio_enabled = 1u;
    std::uint32_t audio_focus_policy = FLY_AUDIO_FOCUS_PAUSE;
    std::uint32_t autosave_enabled = 1u;
    std::string locale_tag{"system"};
    std::string last_played_id;
};

} // namespace flynes::app

#endif
