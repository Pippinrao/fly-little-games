#ifndef FLYNES_PRODUCT_DUAL_START_IDENTITY_HPP
#define FLYNES_PRODUCT_DUAL_START_IDENTITY_HPP

#include <array>
#include <cstdint>

namespace flynes::product {

struct DualStartIdentity final
{
    std::array<std::uint8_t, 32> core_id{};
    std::array<std::uint8_t, 32> profile_id{};
    std::array<std::uint8_t, 32> options_id{};
};

// The shared required-start contract, not an observation of a loaded ROM's
// region or controller topology. Consumers must use its fixed 48000 Hz options
// and independently verify that the loaded content supports the DUAL inputs.
DualStartIdentity canonical_dual_start_identity_v1() noexcept;

} // namespace flynes::product
#endif
