#ifndef FLYNES_SESSION_WIRE_SHA256_HPP
#define FLYNES_SESSION_WIRE_SHA256_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::session::wire {

std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t size);
std::array<std::uint8_t, 32> domain_hash(const char* domain,
                                         const std::uint8_t* data,
                                         std::size_t size);

} // namespace flynes::session::wire

#endif
