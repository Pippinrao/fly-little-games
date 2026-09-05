#pragma once

#include <array>
#include <cstddef>

namespace flynes::product {

enum class PauseCommand
{
    Resume,
    GameCenter,
    Settings,
};

inline constexpr PauseCommand kPauseDrawerCommands[] = {
    PauseCommand::Resume,
    PauseCommand::GameCenter,
    PauseCommand::Settings,
};

static_assert(std::size(kPauseDrawerCommands) == 3,
              "pause drawer is exactly resume, game center, settings");

} // namespace flynes::product
