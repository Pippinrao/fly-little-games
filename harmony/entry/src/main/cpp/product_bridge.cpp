#include "product_bridge.hpp"

#include "flynes/product/control_layout.hpp"
#include "flynes/product/game_center_item.hpp"
#include "flynes/product/game_center_state.hpp"
#include "flynes/product/gamepad_hit_map.hpp"
#include "flynes/product/pause_actions.hpp"

#include <stdexcept>

namespace flynes::harmony {
namespace {

const char* control_id(flynes::product::Control control)
{
    using flynes::product::Control;
    switch (control)
    {
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
    case Control::None:
        return "NONE";
    }
    return "NONE";
}

const char* shape_id(flynes::product::GamepadHitMap::Shape shape)
{
    using flynes::product::GamepadHitMap;
    switch (shape)
    {
    case GamepadHitMap::Shape::Circle:
        return "CIRCLE";
    case GamepadHitMap::Shape::RoundedSquare:
        return "ROUNDED_SQUARE";
    case GamepadHitMap::Shape::Pill:
        return "PILL";
    }
    return "CIRCLE";
}

const char* pause_command_id(flynes::product::PauseCommand command)
{
    using flynes::product::PauseCommand;
    switch (command)
    {
    case PauseCommand::Resume:
        return "resume";
    case PauseCommand::GameCenter:
        return "game_center";
    case PauseCommand::Settings:
        return "settings";
    }
    return "resume";
}

flynes::product::DirectionControlMode direction_mode_from_int(int value)
{
    using flynes::product::DirectionControlMode;
    switch (value)
    {
    case 0:
        return DirectionControlMode::Joystick;
    case 1:
        return DirectionControlMode::FixedJoystick;
    case 2:
        return DirectionControlMode::DPad;
    default:
        throw std::invalid_argument("invalid direction mode");
    }
}

} // namespace

void apply_game_title(GameCenterRow& row, const fly_game_title& title)
{
    if (row.builtin) {
        row.title_en = "From Below";
        row.title_zh_hans = "来自下方";
    } else if (title.match_kind != 0) {
        if (title.title_en_utf8 != nullptr && title.title_en_utf8[0] != '\0')
            row.title_en = title.title_en_utf8;
        if (title.title_zh_hans_utf8 != nullptr && title.title_zh_hans_utf8[0] != '\0')
            row.title_zh_hans = title.title_zh_hans_utf8;
    }
    if (title.match_kind != 0 && title.aliases_utf8 != nullptr)
        row.search_aliases = title.aliases_utf8;
}

std::vector<GameCenterRow> game_center_filter(const std::vector<GameCenterRow>& rows,
                                              const std::string& category,
                                              const std::string& query)
{
    using flynes::product::GameCenterItem;
    using flynes::product::GameCenterState;

    std::vector<GameCenterItem> items;
    items.reserve(rows.size());
    for (const GameCenterRow& row : rows)
    {
        items.emplace_back(row.canonical_id,
                           row.title_en,
                           row.title_zh_hans,
                           row.builtin,
                           row.favorite,
                           row.last_played_sequence,
                           row.original_filename + "\n" + row.search_aliases,
                           row.popularity_score);
    }

    GameCenterState state = GameCenterState::restore(category, query, {});
    const std::vector<GameCenterItem> filtered = state.filtered(items);

    std::vector<GameCenterRow> result;
    result.reserve(filtered.size());
    for (const GameCenterItem& item : filtered)
    {
        GameCenterRow row;
        row.canonical_id = item.canonical_id;
        row.title_en = item.title_en;
        row.title_zh_hans = item.title_zh_hans;
        row.builtin = item.builtin;
        row.favorite = item.favorite;
        row.last_played_sequence = item.last_played_sequence;
        row.original_filename = item.original_filename;
        row.popularity_score = item.popularity_score;
        for (const GameCenterRow& source : rows)
        {
            if (source.canonical_id == item.canonical_id)
            {
                row.source_uuid_hex = source.source_uuid_hex;
                row.source_relative_path = source.source_relative_path;
                row.package_format = source.package_format;
                row.original_filename = source.original_filename;
                row.search_aliases = source.search_aliases;
                break;
            }
        }
        result.push_back(row);
    }
    return result;
}

std::string control_layout_recommended()
{
    return flynes::product::ControlLayoutV2::recommended().encode();
}

std::string control_layout_decode_or_recommended(const std::string& value)
{
    return flynes::product::ControlLayoutV2::decode_or_recommended(value).encode();
}

HitMapDto hit_map_from_layout(int width,
                              int height,
                              float density,
                              int inset_left,
                              int inset_right,
                              int inset_top,
                              int inset_bottom,
                              const std::string& layout_utf8,
                              int direction_mode,
                              float dead_zone)
{
    using flynes::product::Control;
    using flynes::product::ControlLayoutV2;
    using flynes::product::GamepadHitMap;

    const ControlLayoutV2 layout = ControlLayoutV2::decode_or_recommended(layout_utf8);
    const GamepadHitMap hit_map = GamepadHitMap::from_layout(width,
                                                             height,
                                                             density,
                                                             inset_left,
                                                             inset_right,
                                                             inset_top,
                                                             inset_bottom,
                                                             layout,
                                                             direction_mode_from_int(direction_mode),
                                                             dead_zone);

    HitMapDto dto;
    dto.dpad_left = hit_map.dpad_bounds().left();
    dto.dpad_top = hit_map.dpad_bounds().top();
    dto.dpad_right = hit_map.dpad_bounds().right();
    dto.dpad_bottom = hit_map.dpad_bounds().bottom();
    dto.direction_mode = direction_mode;
    dto.dead_zone = hit_map.dead_zone();
    dto.joystick_radius = hit_map.joystick_radius();
    dto.joystick_travel_radius = hit_map.joystick_travel_radius();
    dto.controls.reserve(hit_map.controls().size());
    for (const GamepadHitMap::Target& target : hit_map.controls())
    {
        if (target.control() == Control::Pause || target.control() == Control::None)
        {
            continue;
        }
        dto.controls.push_back(HitMapControlDto{control_id(target.control()),
                                                target.center_x(),
                                                target.center_y(),
                                                target.width(),
                                                target.height(),
                                                shape_id(target.shape())});
    }
    return dto;
}

std::vector<std::string> pause_commands()
{
    std::vector<std::string> commands;
    commands.reserve(std::size(flynes::product::kPauseDrawerCommands));
    for (const flynes::product::PauseCommand command : flynes::product::kPauseDrawerCommands)
    {
        commands.emplace_back(pause_command_id(command));
    }
    return commands;
}

} // namespace flynes::harmony
