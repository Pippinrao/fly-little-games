#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <flynes/flynes_app.h>

namespace flynes::harmony {

struct GameCenterRow final
{
    std::string canonical_id;
    std::string title_en;
    std::string title_zh_hans;
    bool builtin = false;
    bool favorite = false;
    std::int64_t last_played_sequence = 0;
    std::string original_filename;
    std::string source_uuid_hex;
    std::string source_relative_path;
    int package_format = 0;
    int popularity_score = -1;
    std::string search_aliases;
};

void apply_game_title(GameCenterRow& row, const fly_game_title& title);

struct HitMapControlDto final
{
    std::string control;
    float center_x = 0.0f;
    float center_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    std::string shape;
};

struct HitMapDto final
{
    float dpad_left = 0.0f;
    float dpad_top = 0.0f;
    float dpad_right = 0.0f;
    float dpad_bottom = 0.0f;
    std::vector<HitMapControlDto> controls;
    int direction_mode = 0;
    float dead_zone = 0.0f;
    float joystick_radius = 0.0f;
    float joystick_travel_radius = 0.0f;
};

[[nodiscard]] std::vector<GameCenterRow> game_center_filter(const std::vector<GameCenterRow>& rows,
                                                            const std::string& category,
                                                            const std::string& query);

[[nodiscard]] std::string control_layout_recommended();

[[nodiscard]] std::string control_layout_decode_or_recommended(const std::string& value);

[[nodiscard]] HitMapDto hit_map_from_layout(int width,
                                            int height,
                                            float density,
                                            int inset_left,
                                            int inset_right,
                                            int inset_top,
                                            int inset_bottom,
                                            const std::string& layout_utf8,
                                            int direction_mode,
                                            float dead_zone);

[[nodiscard]] std::vector<std::string> pause_commands();

} // namespace flynes::harmony
