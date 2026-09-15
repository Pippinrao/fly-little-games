#ifndef FLYNES_SESSION_WIRE_P256_POINT_HPP
#define FLYNES_SESSION_WIRE_P256_POINT_HPP

#include <cstdint>

namespace flynes::session::wire {

// Validates the exact SEC1/X9.63 uncompressed representation against the
// NIST P-256 field equation. This is syntax/curve validation for public data;
// it does not authenticate the owner of the point.
bool validate_p256_uncompressed_point(
    const std::uint8_t point_x963[65]) noexcept;

bool validate_p256_uncompressed_point_callback(
    void*, const std::uint8_t point_x963[65]) noexcept;

} // namespace flynes::session::wire

#endif
