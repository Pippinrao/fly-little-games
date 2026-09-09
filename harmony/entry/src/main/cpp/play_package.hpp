#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace flynes::harmony {

/** RAW packages return the physical bytes. ZIP packages inflate the named entry. */
std::vector<std::uint8_t> decode_rom_package(const std::uint8_t* bytes,
                                             std::size_t size,
                                             std::uint32_t package_format,
                                             const std::string& zip_entry_utf8);

} // namespace flynes::harmony
