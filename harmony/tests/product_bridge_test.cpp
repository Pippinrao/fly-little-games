#include "product_bridge.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<flynes::harmony::GameCenterRow> task1_items()
{
    return {
        {"builtin", "From Below", "", true, false, 0, "from_below.nes"},
        {"mario", "Super Mario Bros.", "超级马里奥", false, true, 7, "mario.nes"},
        {"contra", "Contra", "魂斗罗", false, false, 0, "contra.zip"},
    };
}

void test_filter_zh_query_selects_contra()
{
    const std::vector<flynes::harmony::GameCenterRow> filtered =
        flynes::harmony::game_center_filter(task1_items(), "ALL", "魂斗");
    expect(filtered.size() == 1, "魂斗 search must return one row");
    if (!filtered.empty())
    {
        expect(filtered[0].canonical_id == "contra", "魂斗 search must select contra");
    }
}

void test_pause_commands_are_exactly_three_snake_ids()
{
    const std::vector<std::string> commands = flynes::harmony::pause_commands();
    expect(commands.size() == 3, "pauseCommands size 3");
    if (commands.size() == 3)
    {
        expect(commands[0] == "resume", "pauseCommands[0] is resume");
        expect(commands[1] == "game_center", "pauseCommands[1] is game_center");
        expect(commands[2] == "settings", "pauseCommands[2] is settings");
    }
}

void test_decode_or_recommended_v1_joy_equals_recommended_encode()
{
    const std::string recommended = flynes::harmony::control_layout_recommended();
    const std::string decoded =
        flynes::harmony::control_layout_decode_or_recommended("v1|JOY");
    expect(!recommended.empty(), "recommended encode is not empty");
    expect(decoded == recommended, "decode_or_recommended(v1|JOY) equals recommended encode");
}

std::string read_text_file(std::string_view relative_path)
{
    std::ifstream input{std::string{relative_path}};
    if (!input.is_open())
    {
        return {};
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void test_checkpoint_bridge_exports_save_and_load_for_per_rom_autosave()
{
    const std::string index_dts = read_text_file(
        "../entry/src/main/cpp/types/libentry/Index.d.ts");
    const std::string napi_init = read_text_file("../entry/src/main/cpp/napi_init.cpp");

    expect(!index_dts.empty(), "Index.d.ts is readable from host test cwd");
    expect(!napi_init.empty(), "napi_init.cpp is readable from host test cwd");
    expect(index_dts.find("playSaveCheckpoint") != std::string::npos,
           "Index.d.ts must export playSaveCheckpoint");
    expect(index_dts.find("playLoadCheckpoint") != std::string::npos,
           "Index.d.ts must export playLoadCheckpoint");
    expect(napi_init.find("\"playSaveCheckpoint\"") != std::string::npos,
           "napi_init.cpp must register playSaveCheckpoint");
    expect(napi_init.find("\"playLoadCheckpoint\"") != std::string::npos,
           "napi_init.cpp must register playLoadCheckpoint");
}

void test_display_protection_bridge_accepts_platform_observations()
{
    const std::string index_dts = read_text_file(
        "../entry/src/main/cpp/types/libentry/Index.d.ts");
    const std::string napi_init = read_text_file("../entry/src/main/cpp/napi_init.cpp");

    expect(index_dts.find("renderSetProtection") != std::string::npos,
           "Index.d.ts must export renderSetProtection");
    expect(napi_init.find("\"renderSetProtection\"") != std::string::npos,
           "napi_init.cpp must register renderSetProtection");
}

void test_hit_map_carries_dpad_and_control_geometry_without_pause()
{
    const std::string layout = flynes::harmony::control_layout_recommended();
    const flynes::harmony::HitMapDto hit_map = flynes::harmony::hit_map_from_layout(
        1920, 1080, 3.0f, 0, 0, 0, 0, layout, 2, 0.22f);

    expect(hit_map.dpad_right > hit_map.dpad_left, "dpad has horizontal extent");
    expect(hit_map.dpad_bottom > hit_map.dpad_top, "dpad has vertical extent");
    expect(!hit_map.controls.empty(), "hit map places controls");
    expect(hit_map.direction_mode == 2, "direction mode is forwarded");

    bool saw_pause = false;
    for (const flynes::harmony::HitMapControlDto& control : hit_map.controls)
    {
        if (control.control == "PAUSE")
        {
            saw_pause = true;
        }
        expect(control.width > 0.0f, "control width is positive");
        expect(control.height > 0.0f, "control height is positive");
        expect(control.shape == "CIRCLE" || control.shape == "ROUNDED_SQUARE"
                   || control.shape == "PILL",
               "control shape is a known hit-map shape");
    }
    expect(!saw_pause, "PAUSE is not placed by the hit map");
}

} // namespace

int main()
{
    test_filter_zh_query_selects_contra();
    test_pause_commands_are_exactly_three_snake_ids();
    test_decode_or_recommended_v1_joy_equals_recommended_encode();
    test_checkpoint_bridge_exports_save_and_load_for_per_rom_autosave();
    test_display_protection_bridge_accepts_platform_observations();
    test_hit_map_carries_dpad_and_control_geometry_without_pause();

    if (failures != 0)
    {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "PASS: Harmony product bridge contract\n";
    return EXIT_SUCCESS;
}
