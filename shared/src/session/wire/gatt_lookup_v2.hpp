#ifndef FLYNES_SESSION_WIRE_GATT_LOOKUP_V2_HPP
#define FLYNES_SESSION_WIRE_GATT_LOOKUP_V2_HPP

#include "session_codec.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::session::wire {

inline constexpr std::size_t kGattLookupRequestBodySizeV2 = 32;
inline constexpr std::size_t kGattLookupMatchBodySizeV2 = 136;
inline constexpr std::size_t kGattLookupRequestSizeV2 = 72;
inline constexpr std::size_t kGattLookupMatchSizeV2 = 176;

enum class GattLookupTypeV2 : std::uint8_t
{
    CodeLookupRequest = 27,
    CodeLookupMatch = 28
};

enum class GattLookupDirection : std::uint8_t
{
    InitiatorToResponder = 1,
    ResponderToInitiator = 2
};

struct GattLookupMessageV2
{
    GattLookupTypeV2 type{};
    const std::uint8_t* body = nullptr;
    std::size_t body_size = 0;
    std::array<std::uint8_t, 32> logical_hash{};
};

[[nodiscard]] bool gatt_lookup_type_allowed_v2(std::uint8_t type,
                                                std::uint8_t outer_version) noexcept;

Status encode_gatt_lookup_request_v2(const std::array<std::uint8_t, 6>& code,
                                     const std::array<std::uint8_t, 16>& request_nonce,
                                     std::uint8_t* out,
                                     std::size_t capacity,
                                     std::size_t* written) noexcept;

Status encode_gatt_lookup_match_v2(const std::array<std::uint8_t, 16>& request_nonce,
                                   const std::array<std::uint8_t, 80>& pair_context,
                                   std::uint8_t* out,
                                   std::size_t capacity,
                                   std::size_t* written) noexcept;

Status decode_gatt_lookup_v2(GattLookupDirection direction,
                             const std::uint8_t* bytes,
                             std::size_t size,
                             GattLookupMessageV2* out) noexcept;

[[nodiscard]] bool lookup_match_binds_request_v2(
    const GattLookupMessageV2& match,
    const std::array<std::uint8_t, 16>& request_nonce,
    const std::array<std::uint8_t, 80>& pair_context) noexcept;

} // namespace flynes::session::wire

#endif
