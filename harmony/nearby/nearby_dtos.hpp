#pragma once

#include <array>
#include <cstdint>

namespace flynes::harmony::nearby {

// Platform-local BLE/QR/Wi-Fi bootstrap DTOs. These must not appear in
// shared/include/flynes/. Radios are not opened by the host fake.
inline constexpr std::array<std::uint8_t, 16> kBleServiceUuid = {
    0xe7, 0x38, 0xdc, 0xda, 0xa2, 0x1a, 0x58, 0x2b,
    0x9c, 0xed, 0xa9, 0x7f, 0x6f, 0xeb, 0xee, 0xdd};

struct NearbyBleAdvertisementDto
{
    std::uint8_t service_uuid[16];
    std::int32_t rssi;
};

struct NearbyQrInviteDto
{
    std::uint8_t pair_context_hash[32];
    std::uint8_t invite_id[16];
};

struct NearbyWifiBearerDto
{
    std::uint8_t selected_plan_hash[32];
    std::uint8_t endpoint_kind;
    std::uint8_t reserved[7];
};

} // namespace flynes::harmony::nearby
