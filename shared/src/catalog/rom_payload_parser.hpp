#ifndef FLYNES_CATALOG_ROM_PAYLOAD_PARSER_HPP
#define FLYNES_CATALOG_ROM_PAYLOAD_PARSER_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::catalog {

enum class RomFormat : std::uint8_t
{
    INES = 0,
    NES2 = 1,
    FDS = 2,
    UNIF = 3,
    UNKNOWN = 4,
};

enum class CompatibilityState : std::uint8_t
{
    PLAYABLE = 0,
    UNSUPPORTED = 1,
    INVALID = 2,
    UNKNOWN = 3,
};

enum class CompatibilityReason : std::uint8_t
{
    PLAYABLE_NES = 0,
    FDS_BIOS_API_NOT_IMPLEMENTED = 1,
    UNIF_PRODUCT_DISABLED = 2,
    NES_HEADER_INVALID = 3,
    NES_ZERO_PRG = 4,
    NES_TRUNCATED = 5,
    NES_SIZE_OVERFLOW = 6,
    FDS_INVALID_HEADER = 7,
    FDS_INVALID_SIDE_COUNT = 8,
    FDS_TRUNCATED = 9,
    UNIF_INVALID_CHUNK = 10,
    UNIF_MISSING_PRG = 11,
    UNKNOWN_FORMAT = 12,
};

enum class RomWarning : std::uint8_t
{
    TRAILING_DATA = 0,
    DIRTY_HEADER = 1,
    UNICODE_PATH_REJECTED = 2,
};

struct ByteView final
{
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

struct RomAnalysis final
{
    std::uint64_t expected_bytes = 0;
    std::uint64_t actual_bytes = 0;
    std::uint64_t prg_bytes = 0;
    std::uint64_t chr_bytes = 0;
    std::int32_t mapper = -1;
    std::int32_t submapper = -1;
    bool trainer = false;
    bool battery = false;
    std::uint64_t disk_sides = 0;
    std::array<RomWarning, 2> warnings{};
    std::size_t warning_count = 0;
};

struct RomParseResult final
{
    bool recognized = false;
    RomFormat format = RomFormat::UNKNOWN;
    CompatibilityState state = CompatibilityState::UNKNOWN;
    CompatibilityReason reason = CompatibilityReason::UNKNOWN_FORMAT;
    RomAnalysis analysis{};
};

RomParseResult parse_rom_payload(ByteView payload) noexcept;

} // namespace flynes::catalog

#endif
