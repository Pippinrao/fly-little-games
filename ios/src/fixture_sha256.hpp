#ifndef FLYNES_IOS_FIXTURE_SHA256_HPP
#define FLYNES_IOS_FIXTURE_SHA256_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace flynes::ios {

/** Test-private one-shot SHA-256 used only to pin the bundled smoke fixture. */
std::string fixture_sha256_hex(const std::uint8_t* data, std::size_t size);

} // namespace flynes::ios

#endif // FLYNES_IOS_FIXTURE_SHA256_HPP
