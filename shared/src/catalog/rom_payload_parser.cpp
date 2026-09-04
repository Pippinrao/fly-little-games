#include "catalog/rom_payload_parser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace flynes::catalog {
namespace {

constexpr std::size_t ines_header_bytes = 16u;
constexpr std::uint64_t trainer_bytes = 512u;
constexpr std::size_t fds_header_bytes = 16u;
constexpr std::size_t fds_side_bytes = 65'500u;
constexpr std::uint64_t signed_64_max =
    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());

constexpr std::array<std::uint8_t, 4> nes_signature = {
    static_cast<std::uint8_t>('N'),
    static_cast<std::uint8_t>('E'),
    static_cast<std::uint8_t>('S'),
    0x1Au,
};
constexpr std::array<std::uint8_t, 4> fds_signature = {
    static_cast<std::uint8_t>('F'),
    static_cast<std::uint8_t>('D'),
    static_cast<std::uint8_t>('S'),
    0x1Au,
};
constexpr std::array<std::uint8_t, 4> unif_signature = {
    static_cast<std::uint8_t>('U'),
    static_cast<std::uint8_t>('N'),
    static_cast<std::uint8_t>('I'),
    static_cast<std::uint8_t>('F'),
};
constexpr std::array<std::uint8_t, 15> fds_side_signature = {
    0x01u,
    static_cast<std::uint8_t>('*'),
    static_cast<std::uint8_t>('N'),
    static_cast<std::uint8_t>('I'),
    static_cast<std::uint8_t>('N'),
    static_cast<std::uint8_t>('T'),
    static_cast<std::uint8_t>('E'),
    static_cast<std::uint8_t>('N'),
    static_cast<std::uint8_t>('D'),
    static_cast<std::uint8_t>('O'),
    static_cast<std::uint8_t>('-'),
    static_cast<std::uint8_t>('H'),
    static_cast<std::uint8_t>('V'),
    static_cast<std::uint8_t>('C'),
    static_cast<std::uint8_t>('*'),
};

static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t),
              "ROM analysis requires size_t to fit in uint64_t");

std::uint64_t byte_count(std::size_t size) noexcept
{
    return static_cast<std::uint64_t>(size);
}

template <std::size_t Size>
bool starts_with_at(ByteView payload,
                    std::size_t offset,
                    const std::array<std::uint8_t, Size>& prefix) noexcept
{
    if (payload.data == nullptr || offset > payload.size || payload.size - offset < Size)
    {
        return false;
    }
    for (std::size_t index = 0; index < Size; ++index)
    {
        if (payload.data[offset + index] != prefix[index])
        {
            return false;
        }
    }
    return true;
}

RomAnalysis basic_analysis(std::size_t actual_size) noexcept
{
    RomAnalysis analysis;
    analysis.expected_bytes = byte_count(actual_size);
    analysis.actual_bytes = byte_count(actual_size);
    return analysis;
}

RomParseResult unrecognized(std::size_t actual_size) noexcept
{
    RomParseResult result;
    result.analysis = basic_analysis(actual_size);
    return result;
}

RomParseResult invalid(RomFormat format,
                       CompatibilityReason reason,
                       const RomAnalysis& analysis) noexcept
{
    RomParseResult result;
    result.recognized = true;
    result.format = format;
    result.state = CompatibilityState::INVALID;
    result.reason = reason;
    result.analysis = analysis;
    return result;
}

void append_warning(RomAnalysis& analysis, RomWarning warning) noexcept
{
    if (analysis.warning_count < analysis.warnings.size())
    {
        analysis.warnings[analysis.warning_count] = warning;
        ++analysis.warning_count;
    }
}

bool checked_signed_add(std::uint64_t left,
                        std::uint64_t right,
                        std::uint64_t& result) noexcept
{
    if (left > signed_64_max || right > signed_64_max || left > signed_64_max - right)
    {
        return false;
    }
    result = left + right;
    return true;
}

bool checked_signed_multiply(std::uint64_t left,
                             std::uint64_t right,
                             std::uint64_t& result) noexcept
{
    if (left != 0u && right > signed_64_max / left)
    {
        return false;
    }
    result = left * right;
    return true;
}

bool nes2_size(std::uint8_t low,
               std::uint8_t high_nibble,
               std::uint64_t unit,
               std::uint64_t& result) noexcept
{
    if (high_nibble != 0x0Fu)
    {
        const std::uint64_t count =
            (static_cast<std::uint64_t>(high_nibble) << 8u) |
            static_cast<std::uint64_t>(low);
        return checked_signed_multiply(count, unit, result);
    }

    const std::uint8_t exponent = static_cast<std::uint8_t>(low >> 2u);
    const std::uint8_t multiplier =
        static_cast<std::uint8_t>(((low & 0x03u) * 2u) + 1u);
    if (exponent >= 63u)
    {
        return false;
    }
    const std::uint64_t base = std::uint64_t{1} << exponent;
    return checked_signed_multiply(base, multiplier, result);
}

bool has_dirty_legacy_header(ByteView payload) noexcept
{
    for (std::size_t index = 12u; index < ines_header_bytes; ++index)
    {
        if (payload.data[index] != 0u)
        {
            return true;
        }
    }
    return false;
}

RomParseResult parse_nes(ByteView payload) noexcept
{
    if (payload.size < ines_header_bytes)
    {
        return invalid(RomFormat::INES,
                       CompatibilityReason::NES_HEADER_INVALID,
                       basic_analysis(payload.size));
    }

    const std::uint32_t flags6 = payload.data[6];
    const std::uint32_t flags7 = payload.data[7];
    const bool is_nes2 = (flags7 & 0x0Cu) == 0x08u;
    const RomFormat format = is_nes2 ? RomFormat::NES2 : RomFormat::INES;
    const bool trainer = (flags6 & 0x04u) != 0u;
    const bool battery = (flags6 & 0x02u) != 0u;
    std::int32_t mapper =
        static_cast<std::int32_t>((flags6 >> 4u) | (flags7 & 0xF0u));
    std::int32_t submapper = 0;
    std::uint64_t prg_bytes = 0;
    std::uint64_t chr_bytes = 0;

    if (is_nes2)
    {
        const std::uint32_t byte8 = payload.data[8];
        const std::uint8_t byte9 = payload.data[9];
        mapper = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(mapper) | ((byte8 & 0x0Fu) << 8u));
        submapper = static_cast<std::int32_t>(byte8 >> 4u);
        if (!nes2_size(payload.data[4],
                       static_cast<std::uint8_t>(byte9 & 0x0Fu),
                       16'384u,
                       prg_bytes) ||
            !nes2_size(payload.data[5],
                       static_cast<std::uint8_t>(byte9 >> 4u),
                       8'192u,
                       chr_bytes))
        {
            RomAnalysis analysis;
            analysis.actual_bytes = byte_count(payload.size);
            analysis.mapper = mapper;
            analysis.submapper = submapper;
            analysis.trainer = trainer;
            analysis.battery = battery;
            return invalid(format, CompatibilityReason::NES_SIZE_OVERFLOW, analysis);
        }
    }
    else
    {
        const bool valid_prg = checked_signed_multiply(payload.data[4], 16'384u, prg_bytes);
        const bool valid_chr = checked_signed_multiply(payload.data[5], 8'192u, chr_bytes);
        if (!valid_prg || !valid_chr)
        {
            RomAnalysis analysis;
            analysis.actual_bytes = byte_count(payload.size);
            analysis.mapper = mapper;
            analysis.submapper = submapper;
            analysis.trainer = trainer;
            analysis.battery = battery;
            return invalid(format, CompatibilityReason::NES_SIZE_OVERFLOW, analysis);
        }
    }

    RomAnalysis analysis;
    analysis.actual_bytes = byte_count(payload.size);
    analysis.prg_bytes = prg_bytes;
    analysis.chr_bytes = chr_bytes;
    analysis.mapper = mapper;
    analysis.submapper = submapper;
    analysis.trainer = trainer;
    analysis.battery = battery;
    if (!is_nes2 && has_dirty_legacy_header(payload))
    {
        append_warning(analysis, RomWarning::DIRTY_HEADER);
        analysis.mapper = static_cast<std::int32_t>(flags6 >> 4u);
    }

    std::uint64_t content_bytes = 0;
    const std::uint64_t prefix_bytes =
        byte_count(ines_header_bytes) + (trainer ? trainer_bytes : 0u);
    if (!checked_signed_add(prg_bytes, chr_bytes, content_bytes) ||
        !checked_signed_add(prefix_bytes, content_bytes, analysis.expected_bytes))
    {
        analysis.expected_bytes = 0;
        return invalid(format, CompatibilityReason::NES_SIZE_OVERFLOW, analysis);
    }
    if (prg_bytes == 0u)
    {
        return invalid(format, CompatibilityReason::NES_ZERO_PRG, analysis);
    }
    if (analysis.actual_bytes < analysis.expected_bytes)
    {
        return invalid(format, CompatibilityReason::NES_TRUNCATED, analysis);
    }
    if (analysis.actual_bytes > analysis.expected_bytes)
    {
        append_warning(analysis, RomWarning::TRAILING_DATA);
    }

    RomParseResult result;
    result.recognized = true;
    result.format = format;
    result.state = CompatibilityState::PLAYABLE;
    result.reason = CompatibilityReason::PLAYABLE_NES;
    result.analysis = analysis;
    return result;
}

RomAnalysis fds_analysis(std::uint64_t expected,
                         std::size_t actual,
                         std::uint64_t sides) noexcept
{
    RomAnalysis analysis;
    analysis.expected_bytes = expected;
    analysis.actual_bytes = byte_count(actual);
    analysis.disk_sides = sides;
    return analysis;
}

bool valid_fds_signatures(ByteView payload,
                          std::size_t offset,
                          std::size_t sides) noexcept
{
    std::size_t side_offset = offset;
    for (std::size_t side = 0; side < sides; ++side)
    {
        if (!starts_with_at(payload, side_offset, fds_side_signature))
        {
            return false;
        }
        if (side + 1u < sides)
        {
            side_offset += fds_side_bytes;
        }
    }
    return true;
}

RomParseResult unsupported_fds(const RomAnalysis& analysis) noexcept
{
    RomParseResult result;
    result.recognized = true;
    result.format = RomFormat::FDS;
    result.state = CompatibilityState::UNSUPPORTED;
    result.reason = CompatibilityReason::FDS_BIOS_API_NOT_IMPLEMENTED;
    result.analysis = analysis;
    return result;
}

RomParseResult parse_headered_fds(ByteView payload) noexcept
{
    if (payload.size < fds_header_bytes)
    {
        return invalid(RomFormat::FDS,
                       CompatibilityReason::FDS_TRUNCATED,
                       basic_analysis(payload.size));
    }
    const std::uint8_t sides = payload.data[4];
    if (sides == 0u)
    {
        return invalid(RomFormat::FDS,
                       CompatibilityReason::FDS_INVALID_SIDE_COUNT,
                       basic_analysis(payload.size));
    }

    const std::uint64_t expected =
        byte_count(fds_header_bytes) +
        static_cast<std::uint64_t>(sides) * byte_count(fds_side_bytes);
    RomAnalysis analysis = fds_analysis(expected, payload.size, sides);
    if (analysis.actual_bytes < expected)
    {
        return invalid(RomFormat::FDS, CompatibilityReason::FDS_TRUNCATED, analysis);
    }
    if (!valid_fds_signatures(payload, fds_header_bytes, sides))
    {
        return invalid(RomFormat::FDS, CompatibilityReason::FDS_INVALID_HEADER, analysis);
    }
    if (analysis.actual_bytes > expected)
    {
        append_warning(analysis, RomWarning::TRAILING_DATA);
    }
    return unsupported_fds(analysis);
}

RomParseResult parse_headerless_fds(ByteView payload) noexcept
{
    if (payload.size < fds_side_bytes || payload.size % fds_side_bytes != 0u)
    {
        std::size_t expected_sides = payload.size / fds_side_bytes;
        if (payload.size % fds_side_bytes != 0u)
        {
            ++expected_sides;
        }
        if (expected_sides == 0u)
        {
            expected_sides = 1u;
        }
        const std::uint64_t sides = byte_count(expected_sides);
        const std::uint64_t side_bytes = byte_count(fds_side_bytes);
        const std::uint64_t expected =
            sides > std::numeric_limits<std::uint64_t>::max() / side_bytes
                ? std::numeric_limits<std::uint64_t>::max()
                : sides * side_bytes;
        return invalid(
            RomFormat::FDS,
            CompatibilityReason::FDS_TRUNCATED,
            fds_analysis(expected, payload.size, sides));
    }

    const std::size_t sides = payload.size / fds_side_bytes;
    RomAnalysis analysis =
        fds_analysis(byte_count(payload.size), payload.size, byte_count(sides));
    if (!valid_fds_signatures(payload, 0u, sides))
    {
        return invalid(RomFormat::FDS, CompatibilityReason::FDS_INVALID_HEADER, analysis);
    }
    return unsupported_fds(analysis);
}

bool is_hex_digit(std::uint8_t value) noexcept
{
    return (value >= static_cast<std::uint8_t>('0') &&
            value <= static_cast<std::uint8_t>('9')) ||
           (value >= static_cast<std::uint8_t>('A') &&
            value <= static_cast<std::uint8_t>('F')) ||
           (value >= static_cast<std::uint8_t>('a') &&
            value <= static_cast<std::uint8_t>('f'));
}

std::uint64_t little_endian_u32(ByteView payload, std::size_t offset) noexcept
{
    return static_cast<std::uint64_t>(payload.data[offset]) |
           (static_cast<std::uint64_t>(payload.data[offset + 1u]) << 8u) |
           (static_cast<std::uint64_t>(payload.data[offset + 2u]) << 16u) |
           (static_cast<std::uint64_t>(payload.data[offset + 3u]) << 24u);
}

RomParseResult parse_unif(ByteView payload) noexcept
{
    if (payload.size < 32u)
    {
        return invalid(RomFormat::UNIF,
                       CompatibilityReason::UNIF_INVALID_CHUNK,
                       basic_analysis(payload.size));
    }

    std::size_t cursor = 32u;
    bool has_prg = false;
    while (cursor < payload.size)
    {
        if (payload.size - cursor < 8u)
        {
            return invalid(RomFormat::UNIF,
                           CompatibilityReason::UNIF_INVALID_CHUNK,
                           basic_analysis(payload.size));
        }
        const bool prg_chunk =
            payload.data[cursor] == static_cast<std::uint8_t>('P') &&
            payload.data[cursor + 1u] == static_cast<std::uint8_t>('R') &&
            payload.data[cursor + 2u] == static_cast<std::uint8_t>('G') &&
            is_hex_digit(payload.data[cursor + 3u]);
        const std::uint64_t length = little_endian_u32(payload, cursor + 4u);
        cursor += 8u;
        const std::uint64_t remaining = byte_count(payload.size - cursor);
        if (length > remaining)
        {
            return invalid(RomFormat::UNIF,
                           CompatibilityReason::UNIF_INVALID_CHUNK,
                           basic_analysis(payload.size));
        }
        if (prg_chunk && length > 0u)
        {
            has_prg = true;
        }
        cursor += static_cast<std::size_t>(length);
    }
    if (!has_prg)
    {
        return invalid(RomFormat::UNIF,
                       CompatibilityReason::UNIF_MISSING_PRG,
                       basic_analysis(payload.size));
    }

    RomParseResult result;
    result.recognized = true;
    result.format = RomFormat::UNIF;
    result.state = CompatibilityState::UNSUPPORTED;
    result.reason = CompatibilityReason::UNIF_PRODUCT_DISABLED;
    result.analysis = basic_analysis(payload.size);
    return result;
}

} // namespace

RomParseResult parse_rom_payload(ByteView payload) noexcept
{
    if (starts_with_at(payload, 0u, nes_signature))
    {
        return parse_nes(payload);
    }
    if (starts_with_at(payload, 0u, fds_signature))
    {
        return parse_headered_fds(payload);
    }
    if (starts_with_at(payload, 0u, fds_side_signature))
    {
        return parse_headerless_fds(payload);
    }
    if (starts_with_at(payload, 0u, unif_signature))
    {
        return parse_unif(payload);
    }
    return unrecognized(payload.size);
}

} // namespace flynes::catalog
