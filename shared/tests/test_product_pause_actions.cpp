#include "flynes/product/pause_actions.hpp"

#include <array>
#include <cstddef>
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

void test_pause_drawer_command_list()
{
    using flynes::product::PauseCommand;
    using flynes::product::kPauseDrawerCommands;

    check(std::size(kPauseDrawerCommands) == 3, "exactly three");
    check(kPauseDrawerCommands[0] == PauseCommand::Resume, "resume first");
    check(kPauseDrawerCommands[1] == PauseCommand::GameCenter, "center");
    check(kPauseDrawerCommands[2] == PauseCommand::Settings, "settings");
}

void test_pause_drawer_excludes_save_and_load()
{
    using flynes::product::PauseCommand;

    check(static_cast<int>(PauseCommand::Resume) == 0, "resume ordinal");
    check(static_cast<int>(PauseCommand::GameCenter) == 1, "game center ordinal");
    check(static_cast<int>(PauseCommand::Settings) == 2, "settings ordinal");
    check(static_cast<int>(PauseCommand::Settings) + 1 == 3, "no save/load enumerators");
}

} // namespace

int main()
{
    test_pause_drawer_command_list();
    test_pause_drawer_excludes_save_and_load();

    if (failures == 0)
    {
        std::puts("flynes_product_pause_actions_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
