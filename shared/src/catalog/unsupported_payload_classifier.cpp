#include "catalog/unsupported_payload_classifier.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::catalog {
namespace {

constexpr std::size_t game_boy_logo_offset = 0x104u;
constexpr std::size_t sidecar_inspection_limit = 4096u;
constexpr std::array<std::uint8_t, 48> game_boy_logo = {
    0xCEu, 0xEDu, 0x66u, 0x66u, 0xCCu, 0x0Du, 0x00u, 0x0Bu,
    0x03u, 0x73u, 0x00u, 0x83u, 0x00u, 0x0Cu, 0x00u, 0x0Du,
    0x00u, 0x08u, 0x11u, 0x1Fu, 0x88u, 0x89u, 0x00u, 0x0Eu,
    0xDCu, 0xCCu, 0x6Eu, 0xE6u, 0xDDu, 0xDDu, 0xD9u, 0x99u,
    0xBBu, 0xBBu, 0x67u, 0x63u, 0x6Eu, 0x0Eu, 0xECu, 0xCCu,
    0xDDu, 0xDCu, 0x99u, 0x9Fu, 0xBBu, 0xB9u, 0x33u, 0x3Eu,
};

bool has_zip_signature(ByteView payload) noexcept
{
    if (payload.size < 4u)
    {
        return false;
    }
    return payload.data[0] == static_cast<std::uint8_t>('P') &&
           payload.data[1] == static_cast<std::uint8_t>('K') &&
           ((payload.data[2] == 0x03u && payload.data[3] == 0x04u) ||
            (payload.data[2] == 0x05u && payload.data[3] == 0x06u) ||
            (payload.data[2] == 0x07u && payload.data[3] == 0x08u));
}

bool has_game_boy_logo(ByteView payload) noexcept
{
    if (payload.size < game_boy_logo_offset ||
        payload.size - game_boy_logo_offset < game_boy_logo.size())
    {
        return false;
    }
    for (std::size_t index = 0u; index < game_boy_logo.size(); ++index)
    {
        if (payload.data[game_boy_logo_offset + index] != game_boy_logo[index])
        {
            return false;
        }
    }
    return true;
}

bool is_sidecar(ByteView payload) noexcept
{
    const std::size_t inspected = std::min(payload.size, sidecar_inspection_limit);
    for (std::size_t index = 0u; index < inspected; ++index)
    {
        const std::uint8_t value = payload.data[index];
        if (value == 0u ||
            (value < 0x20u && value != static_cast<std::uint8_t>('\t') &&
             value != static_cast<std::uint8_t>('\n') &&
             value != static_cast<std::uint8_t>('\r')))
        {
            return false;
        }
    }
    return true;
}

} // namespace

UnsupportedPayloadReason classify_unsupported_payload(ByteView payload) noexcept
{
    if (payload.data == nullptr && payload.size != 0u)
    {
        return UnsupportedPayloadReason::UNKNOWN_FORMAT;
    }
    if (has_zip_signature(payload))
    {
        return UnsupportedPayloadReason::NESTED_ARCHIVE;
    }
    if (payload.size >= 2u && payload.data[0] == static_cast<std::uint8_t>('M') &&
        payload.data[1] == static_cast<std::uint8_t>('Z'))
    {
        return UnsupportedPayloadReason::EXECUTABLE;
    }
    if (has_game_boy_logo(payload))
    {
        return UnsupportedPayloadReason::GAME_BOY;
    }
    return is_sidecar(payload) ? UnsupportedPayloadReason::SIDECAR :
                                 UnsupportedPayloadReason::UNKNOWN_FORMAT;
}

} // namespace flynes::catalog
