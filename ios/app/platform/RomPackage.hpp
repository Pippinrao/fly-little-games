#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace flynes::ios {
// Uses catalog identity, including raw ZIP filename and local-header offset.
std::vector<std::uint8_t> resolve_rom_package(
    const std::uint8_t* bytes, std::size_t size, std::uint32_t format,
    const std::string& source_id, const std::string& relative_path,
    const std::string& variant_id, const std::string& payload_sha256,
    const std::string& physical_sha256);
}
