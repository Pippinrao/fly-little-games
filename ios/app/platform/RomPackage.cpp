#include "RomPackage.hpp"
#include "catalog/bounded_zip_archive.hpp"
#include "catalog/content_identity.hpp"
#include <flynes/flynes_app.h>
#include <stdexcept>

namespace flynes::ios {
namespace {
std::string hex(const std::vector<std::uint8_t>& bytes) {
    std::string result;
    result.reserve(bytes.size() * 2);
    for (auto byte : bytes) {
        result += "0123456789ABCDEF"[byte >> 4];
        result += "0123456789ABCDEF"[byte & 15];
    }
    return result;
}
}

std::vector<std::uint8_t> resolve_rom_package(
    const std::uint8_t* bytes, std::size_t size, std::uint32_t format,
    const std::string& source_id, const std::string& relative_path,
    const std::string& selected_variant, const std::string& payload_sha256,
    const std::string& physical_sha256)
{
    using namespace flynes::catalog;
    const auto limits = BoundedZipLimits::defaults();
    if (bytes == nullptr || size == 0 || size > limits.max_package_bytes())
        throw std::runtime_error("ROM package is empty or exceeds the scan limit");
    const auto physical = sha256_hex({bytes, size});
    if (!physical.ok() || physical.value != physical_sha256)
        throw std::runtime_error("ROM package changed; rescan the source");
    const auto package = package_id(source_id, relative_path);
    if (!package.ok()) throw std::runtime_error("Invalid package identity");
    auto matches = [&](const std::uint8_t* data, std::size_t count, std::string_view locator) {
        const auto digest = sha256_hex({data, count});
        if (!digest.ok() || digest.value != payload_sha256) return false;
        const auto variant = variant_id(package.value, locator, digest.value);
        return variant.ok() && variant.value == selected_variant;
    };
    if (format == FLY_PACKAGE_FORMAT_RAW) {
        if (size > limits.max_payload_bytes() || !matches(bytes, size, RAW_LOCATOR))
            throw std::runtime_error("ROM content does not match the selected game");
        return {bytes, bytes + size};
    }
    if (format != FLY_PACKAGE_FORMAT_ZIP)
        throw std::runtime_error("Unsupported ROM package format");
    auto opened = open_bounded_zip(bytes, size, limits);
    if (!opened.succeeded() || opened.archive() == nullptr)
        throw std::runtime_error("Invalid or oversized ZIP package");
    const auto& archive = *opened.archive();
    for (const auto& entry : archive.entries()) {
        if (entry.is_directory) continue;
        const auto locator = hex(entry.raw_name) + "@" + std::to_string(entry.local_header_offset);
        // Match the stored variant before inflating, including non-UTF8 names.
        const auto variant = variant_id(package.value, locator, payload_sha256);
        if (!variant.ok() || variant.value != selected_variant) continue;
        auto payload = read_bounded_zip_payload(archive, entry.raw_name, entry.local_header_offset);
        if (!payload.succeeded() || payload.payload() == nullptr)
            throw std::runtime_error("Selected ZIP entry could not be decoded");
        const auto& data = *payload.payload();
        if (!matches(data.data(), data.size(), locator))
            throw std::runtime_error("Selected ZIP entry content changed");
        return data;
    }
    throw std::runtime_error("Selected ZIP entry no longer exists");
}
}
