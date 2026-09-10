#pragma once

#include <cstdint>

namespace flynes::product {

constexpr std::uint32_t NES_A = 0x01;
constexpr std::uint32_t NES_B = 0x02;
constexpr std::uint32_t NES_SELECT = 0x04;
constexpr std::uint32_t NES_START = 0x08;
constexpr std::uint32_t NES_UP = 0x10;
constexpr std::uint32_t NES_DOWN = 0x20;
constexpr std::uint32_t NES_LEFT = 0x40;
constexpr std::uint32_t NES_RIGHT = 0x80;

} // namespace flynes::product
