#include "play_package.hpp"

#include "catalog/bounded_zip_archive.hpp"

#include <flynes/flynes_app.h>

#include <stdexcept>

namespace flynes::harmony {
namespace {

std::string zip_entry_name(const flynes::catalog::BoundedZipEntry& entry)
{
    std::string name(reinterpret_cast<const char*>(entry.raw_name.data()), entry.raw_name.size());
    for (char& ch : name)
    {
        if (ch == '\\')
        {
            ch = '/';
        }
    }
    return name;
}

} // namespace

std::vector<std::uint8_t> decode_rom_package(const std::uint8_t* bytes,
                                             std::size_t size,
                                             std::uint32_t package_format,
                                             const std::string& zip_entry_utf8)
{
    if (bytes == nullptr || size == 0)
    {
        throw std::runtime_error("ROM package is empty");
    }
    if (package_format != FLY_PACKAGE_FORMAT_ZIP)
    {
        return std::vector<std::uint8_t>(bytes, bytes + size);
    }
    if (zip_entry_utf8.empty())
    {
        throw std::runtime_error("ZIP ROM is missing an entry name");
    }

    auto opened = flynes::catalog::open_bounded_zip(
        bytes, size, flynes::catalog::BoundedZipLimits::defaults());
    if (!opened.succeeded() || opened.archive() == nullptr)
    {
        const flynes::catalog::BoundedZipOpenError* error = opened.error();
        throw std::runtime_error(error == nullptr ? "invalid ZIP ROM" : error->message);
    }
    const flynes::catalog::BoundedZipArchive& archive = *opened.archive();
    for (const flynes::catalog::BoundedZipEntry& entry : archive.entries())
    {
        if (entry.is_directory)
        {
            continue;
        }
        if (zip_entry_name(entry) != zip_entry_utf8)
        {
            continue;
        }
        auto payload = flynes::catalog::read_bounded_zip_payload(
            archive, entry.raw_name, entry.local_header_offset);
        if (!payload.succeeded() || payload.payload() == nullptr)
        {
            const flynes::catalog::BoundedZipOpenError* error = payload.error();
            throw std::runtime_error(error == nullptr ? "ZIP entry inflate failed" : error->message);
        }
        return *payload.payload();
    }
    throw std::runtime_error("ZIP ROM entry was not found");
}

} // namespace flynes::harmony
