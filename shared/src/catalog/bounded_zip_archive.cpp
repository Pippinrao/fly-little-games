#include "catalog/bounded_zip_archive.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace flynes::catalog {
namespace {

constexpr std::uint32_t local_signature = 0x04034B50u;
constexpr std::uint32_t central_signature = 0x02014B50u;
constexpr std::uint32_t end_signature = 0x06054B50u;
constexpr std::uint32_t descriptor_signature = 0x08074B50u;
constexpr std::uint16_t encrypted_flag = 0x0001u;
constexpr std::uint16_t data_descriptor_flag = 0x0008u;
constexpr std::uint16_t efs_flag = 0x0800u;

class ValidationFailure final : public std::exception
{
public:
    ValidationFailure(ZipOpenCode code, std::string message)
        : code_(code), message_(std::move(message))
    {
    }

    const char* what() const noexcept override
    {
        return message_.c_str();
    }

    ZipOpenCode code() const noexcept
    {
        return code_;
    }

private:
    ZipOpenCode code_;
    std::string message_;
};

[[noreturn]] void fail(ZipOpenCode code, const char* message)
{
    throw ValidationFailure(code, message);
}

[[noreturn]] void invalid(const char* message)
{
    fail(ZipOpenCode::INVALID_ZIP, message);
}

void require_range(const std::vector<std::uint8_t>& bytes,
                   std::uint64_t offset,
                   std::uint64_t length,
                   const char* message)
{
    const std::uint64_t size = static_cast<std::uint64_t>(bytes.size());
    if (offset > size || length > size - offset)
    {
        invalid(message);
    }
}

std::uint16_t unsigned_short(const std::vector<std::uint8_t>& bytes, std::uint64_t offset)
{
    require_range(bytes, offset, 2u, "ZIP structure is truncated");
    const std::size_t start = static_cast<std::size_t>(offset);
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[start]) |
        (static_cast<std::uint16_t>(bytes[start + 1u]) << 8u));
}

std::uint32_t unsigned_int(const std::vector<std::uint8_t>& bytes, std::uint64_t offset)
{
    require_range(bytes, offset, 4u, "ZIP structure is truncated");
    const std::size_t start = static_cast<std::size_t>(offset);
    return static_cast<std::uint32_t>(bytes[start]) |
           (static_cast<std::uint32_t>(bytes[start + 1u]) << 8u) |
           (static_cast<std::uint32_t>(bytes[start + 2u]) << 16u) |
           (static_cast<std::uint32_t>(bytes[start + 3u]) << 24u);
}

std::uint64_t checked_add(std::uint64_t first,
                          std::uint64_t second,
                          const char* message)
{
    if (first > std::numeric_limits<std::uint64_t>::max() - second)
    {
        invalid(message);
    }
    return first + second;
}

bool is_continuation(std::uint8_t value)
{
    return value >= 0x80u && value <= 0xBFu;
}

bool is_strict_utf8(const std::vector<std::uint8_t>& value)
{
    std::size_t index = 0u;
    while (index < value.size())
    {
        const std::uint8_t first = value[index];
        if (first <= 0x7Fu)
        {
            ++index;
            continue;
        }
        if (first >= 0xC2u && first <= 0xDFu)
        {
            if (index + 1u >= value.size() || !is_continuation(value[index + 1u]))
            {
                return false;
            }
            index += 2u;
            continue;
        }
        if (first >= 0xE0u && first <= 0xEFu)
        {
            if (index + 2u >= value.size() || !is_continuation(value[index + 2u]))
            {
                return false;
            }
            const std::uint8_t second = value[index + 1u];
            const bool second_valid =
                (first == 0xE0u && second >= 0xA0u && second <= 0xBFu) ||
                (first == 0xEDu && second >= 0x80u && second <= 0x9Fu) ||
                ((first >= 0xE1u && first <= 0xECu) && is_continuation(second)) ||
                ((first >= 0xEEu && first <= 0xEFu) && is_continuation(second));
            if (!second_valid)
            {
                return false;
            }
            index += 3u;
            continue;
        }
        if (first >= 0xF0u && first <= 0xF4u)
        {
            if (index + 3u >= value.size() || !is_continuation(value[index + 2u]) ||
                !is_continuation(value[index + 3u]))
            {
                return false;
            }
            const std::uint8_t second = value[index + 1u];
            const bool second_valid =
                (first == 0xF0u && second >= 0x90u && second <= 0xBFu) ||
                ((first >= 0xF1u && first <= 0xF3u) && is_continuation(second)) ||
                (first == 0xF4u && second >= 0x80u && second <= 0x8Fu);
            if (!second_valid)
            {
                return false;
            }
            index += 4u;
            continue;
        }
        return false;
    }
    return true;
}

struct CentralEntry final
{
    std::vector<std::uint8_t> raw_name;
    std::vector<std::uint8_t> central_extra;
    std::int32_t local_header_offset = 0;
    std::uint16_t flags = 0;
    std::uint16_t method = 0;
    std::uint32_t crc32 = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
};

std::uint64_t find_end_record(const std::vector<std::uint8_t>& archive)
{
    if (archive.size() < 22u)
    {
        invalid("ZIP end record is missing");
    }
    const std::uint64_t size = static_cast<std::uint64_t>(archive.size());
    const std::uint64_t last = size - 22u;
    const std::uint64_t scan_distance = 22u + 0xFFFFu;
    const std::uint64_t earliest = size > scan_distance ? size - scan_distance : 0u;
    std::uint64_t offset = last;
    while (true)
    {
        if (unsigned_int(archive, offset) == end_signature)
        {
            const std::uint16_t comment_length = unsigned_short(archive, offset + 20u);
            if (offset + 22u + comment_length == size)
            {
                return offset;
            }
        }
        if (offset == earliest)
        {
            break;
        }
        --offset;
    }
    invalid("ZIP end record is missing or truncated");
}

void validate_ratio(const CentralEntry& entry, const BoundedZipLimits& limits)
{
    if (entry.uncompressed_size <= limits.ratio_guard_threshold_bytes())
    {
        return;
    }
    const std::uint64_t maximum =
        static_cast<std::uint64_t>(entry.compressed_size) * limits.max_compression_ratio();
    if (entry.compressed_size == 0u || entry.uncompressed_size > maximum)
    {
        fail(ZipOpenCode::RATIO_LIMIT_EXCEEDED,
             "ZIP compression ratio exceeds the limit");
    }
}

void require_zero_or_equal(std::uint32_t local,
                           std::uint32_t central,
                           const char* field)
{
    if (local == 0u || local == central)
    {
        return;
    }
    if (std::string(field) == "CRC")
    {
        invalid("ZIP local and central CRC differ");
    }
    if (std::string(field) == "compressed size")
    {
        invalid("ZIP local and central compressed size differ");
    }
    invalid("ZIP local and central size differ");
}

std::uint64_t validate_local_header(const std::vector<std::uint8_t>& archive,
                                    const CentralEntry& central,
                                    std::uint64_t central_offset)
{
    const std::uint64_t offset = static_cast<std::uint32_t>(central.local_header_offset);
    require_range(archive, offset, 30u, "ZIP local header is truncated");
    if (unsigned_int(archive, offset) != local_signature)
    {
        invalid("ZIP local header signature is invalid");
    }
    const std::uint16_t local_flags = unsigned_short(archive, offset + 6u);
    const std::uint16_t local_method = unsigned_short(archive, offset + 8u);
    if (local_flags != central.flags || local_method != central.method)
    {
        invalid("ZIP local and central flags or methods differ");
    }
    const std::uint16_t name_length = unsigned_short(archive, offset + 26u);
    const std::uint16_t extra_length = unsigned_short(archive, offset + 28u);
    if (static_cast<std::size_t>(name_length) != central.raw_name.size())
    {
        invalid("ZIP local and central entry names differ");
    }
    const std::uint64_t metadata_length =
        static_cast<std::uint64_t>(name_length) + extra_length;
    require_range(archive, offset + 30u, metadata_length,
                  "ZIP local entry metadata is truncated");
    const std::size_t local_name = static_cast<std::size_t>(offset + 30u);
    for (std::size_t index = 0u; index < central.raw_name.size(); ++index)
    {
        if (archive[local_name + index] != central.raw_name[index])
        {
            invalid("ZIP local and central entry names differ");
        }
    }
    const std::uint64_t data_offset = checked_add(
        offset + 30u, metadata_length, "ZIP local data offset overflows");
    const std::uint64_t payload_end = checked_add(
        data_offset, central.compressed_size, "ZIP entry size overflows");
    if (payload_end > central_offset ||
        data_offset > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
    {
        invalid("ZIP compressed payload is truncated");
    }

    const std::uint32_t local_crc = unsigned_int(archive, offset + 14u);
    const std::uint32_t local_compressed = unsigned_int(archive, offset + 18u);
    const std::uint32_t local_uncompressed = unsigned_int(archive, offset + 22u);
    if ((local_flags & data_descriptor_flag) != 0u)
    {
        require_zero_or_equal(local_crc, central.crc32, "CRC");
        require_zero_or_equal(local_compressed, central.compressed_size, "compressed size");
        require_zero_or_equal(local_uncompressed, central.uncompressed_size, "size");
    }
    else if (local_crc != central.crc32 ||
             local_compressed != central.compressed_size ||
             local_uncompressed != central.uncompressed_size)
    {
        invalid("ZIP local and central CRC or sizes differ");
    }

    std::uint64_t entry_end = payload_end;
    if ((local_flags & data_descriptor_flag) != 0u)
    {
        require_range(archive, payload_end, 12u, "ZIP data descriptor is truncated");
        const std::uint32_t first = unsigned_int(archive, payload_end);
        const bool is_signed = first == descriptor_signature;
        const std::uint64_t values_offset = payload_end + (is_signed ? 4u : 0u);
        if (is_signed)
        {
            require_range(archive, payload_end, 16u,
                          "ZIP signed data descriptor is truncated");
        }
        const std::uint32_t descriptor_crc = unsigned_int(archive, values_offset);
        const std::uint32_t descriptor_compressed = unsigned_int(archive, values_offset + 4u);
        const std::uint32_t descriptor_uncompressed = unsigned_int(archive, values_offset + 8u);
        if (descriptor_crc != central.crc32 ||
            descriptor_compressed != central.compressed_size ||
            descriptor_uncompressed != central.uncompressed_size)
        {
            invalid("ZIP data descriptor differs from central metadata");
        }
        entry_end = payload_end + (is_signed ? 16u : 12u);
        if (entry_end > central_offset)
        {
            invalid("ZIP data descriptor overlaps the central directory");
        }
    }
    return entry_end;
}

std::vector<BoundedZipEntry> parse_entries(const std::vector<std::uint8_t>& archive,
                                           const BoundedZipLimits& limits)
{
    const std::uint64_t end_record = find_end_record(archive);
    const std::uint16_t disk_number = unsigned_short(archive, end_record + 4u);
    const std::uint16_t central_directory_disk = unsigned_short(archive, end_record + 6u);
    const std::uint16_t entries_on_disk = unsigned_short(archive, end_record + 8u);
    const std::uint16_t total_entries = unsigned_short(archive, end_record + 10u);
    const std::uint32_t central_size = unsigned_int(archive, end_record + 12u);
    const std::uint32_t central_offset = unsigned_int(archive, end_record + 16u);
    if (total_entries > limits.max_zip_entries())
    {
        fail(ZipOpenCode::ENTRY_LIMIT_EXCEEDED, "ZIP entry count exceeds the limit");
    }
    if (disk_number != 0u || central_directory_disk != 0u ||
        entries_on_disk != total_entries)
    {
        invalid("split ZIP archives are unsupported");
    }
    if (total_entries == 0xFFFFu || central_size == 0xFFFFFFFFu ||
        central_offset == 0xFFFFFFFFu)
    {
        invalid("ZIP64 archives are unsupported");
    }
    const std::uint64_t central_end = checked_add(
        central_offset, central_size, "central directory overflows");
    if (central_end != end_record ||
        central_offset > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
    {
        invalid("ZIP central directory bounds are invalid");
    }

    std::vector<CentralEntry> central_entries;
    central_entries.reserve(total_entries);
    std::uint64_t cursor = central_offset;
    for (std::uint32_t index = 0u; index < total_entries; ++index)
    {
        require_range(archive, cursor, 46u, "ZIP central directory is truncated");
        if (unsigned_int(archive, cursor) != central_signature)
        {
            invalid("ZIP central directory signature is invalid");
        }
        const std::uint16_t flags = unsigned_short(archive, cursor + 8u);
        const std::uint16_t method = unsigned_short(archive, cursor + 10u);
        const std::uint16_t name_length = unsigned_short(archive, cursor + 28u);
        const std::uint16_t extra_length = unsigned_short(archive, cursor + 30u);
        const std::uint16_t comment_length = unsigned_short(archive, cursor + 32u);
        if (name_length == 0u)
        {
            invalid("ZIP entry name is empty");
        }
        if (name_length > limits.max_name_bytes())
        {
            fail(ZipOpenCode::NAME_LIMIT_EXCEEDED, "ZIP entry name exceeds the limit");
        }
        if (unsigned_short(archive, cursor + 34u) != 0u)
        {
            invalid("split ZIP entries are unsupported");
        }
        const std::uint32_t local_offset = unsigned_int(archive, cursor + 42u);
        if (local_offset == 0xFFFFFFFFu ||
            local_offset > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
        {
            invalid("ZIP64 local headers are unsupported");
        }
        const std::uint64_t variable_length =
            static_cast<std::uint64_t>(name_length) + extra_length + comment_length;
        const std::uint64_t central_entry_end = checked_add(
            cursor + 46u, variable_length, "ZIP central entry overflows");
        if (central_entry_end > central_end)
        {
            invalid("ZIP central entry is truncated");
        }

        CentralEntry entry;
        const std::size_t name_begin = static_cast<std::size_t>(cursor + 46u);
        entry.raw_name.assign(archive.begin() + static_cast<std::ptrdiff_t>(name_begin),
                              archive.begin() + static_cast<std::ptrdiff_t>(
                                  name_begin + name_length));
        const std::size_t extra_begin = name_begin + name_length;
        entry.central_extra.assign(
            archive.begin() + static_cast<std::ptrdiff_t>(extra_begin),
            archive.begin() + static_cast<std::ptrdiff_t>(extra_begin + extra_length));
        if ((flags & encrypted_flag) != 0u)
        {
            fail(ZipOpenCode::ENCRYPTED, "encrypted ZIP entries are unsupported");
        }
        if (method != 0u && method != 8u)
        {
            fail(ZipOpenCode::UNSUPPORTED_COMPRESSION,
                 "ZIP compression method is unsupported");
        }
        if ((flags & efs_flag) != 0u && !is_strict_utf8(entry.raw_name))
        {
            invalid("ZIP entry name encoding is invalid");
        }
        entry.local_header_offset = static_cast<std::int32_t>(local_offset);
        entry.flags = flags;
        entry.method = method;
        entry.crc32 = unsigned_int(archive, cursor + 16u);
        entry.compressed_size = unsigned_int(archive, cursor + 20u);
        entry.uncompressed_size = unsigned_int(archive, cursor + 24u);
        validate_ratio(entry, limits);
        central_entries.push_back(std::move(entry));
        cursor = central_entry_end;
    }
    if (cursor != central_end)
    {
        invalid("ZIP central entry count is invalid");
    }

    std::stable_sort(
        central_entries.begin(),
        central_entries.end(),
        [](const CentralEntry& first, const CentralEntry& second) {
            return first.local_header_offset < second.local_header_offset;
        });
    std::uint64_t declared_inflated = 0u;
    std::optional<std::uint64_t> previous_entry_end;
    for (const CentralEntry& central : central_entries)
    {
        const std::uint64_t local_offset =
            static_cast<std::uint32_t>(central.local_header_offset);
        if (previous_entry_end.has_value() && *previous_entry_end > local_offset)
        {
            invalid("ZIP local entries overlap");
        }
        previous_entry_end = validate_local_header(archive, central, central_offset);
        declared_inflated = checked_add(
            declared_inflated, central.uncompressed_size, "ZIP inflated total overflows");
        if (declared_inflated > limits.max_cumulative_inflated_bytes())
        {
            fail(ZipOpenCode::INFLATED_LIMIT_EXCEEDED,
                 "ZIP inflated total exceeds the limit");
        }
    }

    std::vector<BoundedZipEntry> entries;
    entries.reserve(central_entries.size());
    for (CentralEntry& central : central_entries)
    {
        BoundedZipEntry entry;
        entry.raw_name = std::move(central.raw_name);
        entry.central_extra = std::move(central.central_extra);
        entry.local_header_offset = central.local_header_offset;
        entry.flags = central.flags;
        entry.method = central.method;
        entry.crc32 = central.crc32;
        entry.compressed_size = central.compressed_size;
        entry.uncompressed_size = central.uncompressed_size;
        entry.is_directory = !entry.raw_name.empty() &&
                             (entry.raw_name.back() == static_cast<std::uint8_t>('/') ||
                              entry.raw_name.back() == static_cast<std::uint8_t>('\\'));
        entries.push_back(std::move(entry));
    }
    return entries;
}

} // namespace

const char* zip_open_code_name(ZipOpenCode code) noexcept
{
    switch (code)
    {
    case ZipOpenCode::INVALID_ZIP: return "INVALID_ZIP";
    case ZipOpenCode::PACKAGE_LIMIT_EXCEEDED: return "PACKAGE_LIMIT_EXCEEDED";
    case ZipOpenCode::ENTRY_LIMIT_EXCEEDED: return "ENTRY_LIMIT_EXCEEDED";
    case ZipOpenCode::INFLATED_LIMIT_EXCEEDED: return "INFLATED_LIMIT_EXCEEDED";
    case ZipOpenCode::PAYLOAD_LIMIT_EXCEEDED: return "PAYLOAD_LIMIT_EXCEEDED";
    case ZipOpenCode::NAME_LIMIT_EXCEEDED: return "NAME_LIMIT_EXCEEDED";
    case ZipOpenCode::RATIO_LIMIT_EXCEEDED: return "RATIO_LIMIT_EXCEEDED";
    case ZipOpenCode::ENCRYPTED: return "ENCRYPTED";
    case ZipOpenCode::UNSUPPORTED_COMPRESSION: return "UNSUPPORTED_COMPRESSION";
    case ZipOpenCode::ENTRY_MISSING: return "ENTRY_MISSING";
    }
    return "INVALID_ZIP";
}

BoundedZipLimits::BoundedZipLimits(std::uint64_t max_package_bytes,
                                   std::uint64_t max_payload_bytes,
                                   std::uint32_t max_zip_entries,
                                   std::uint64_t max_cumulative_inflated_bytes,
                                   std::uint16_t max_name_bytes,
                                   std::uint32_t max_compression_ratio,
                                   std::uint64_t ratio_guard_threshold_bytes) noexcept
    : max_package_bytes_(max_package_bytes),
      max_payload_bytes_(max_payload_bytes),
      max_zip_entries_(max_zip_entries),
      max_cumulative_inflated_bytes_(max_cumulative_inflated_bytes),
      max_name_bytes_(max_name_bytes),
      max_compression_ratio_(max_compression_ratio),
      ratio_guard_threshold_bytes_(ratio_guard_threshold_bytes)
{
}

std::optional<BoundedZipLimits> BoundedZipLimits::create(
    std::uint64_t max_package_bytes,
    std::uint64_t max_payload_bytes,
    std::uint64_t max_zip_entries,
    std::uint64_t max_cumulative_inflated_bytes,
    std::uint64_t max_name_bytes,
    std::uint64_t max_compression_ratio,
    std::uint64_t ratio_guard_threshold_bytes) noexcept
{
    constexpr std::uint64_t java_long_max =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    constexpr std::uint64_t java_int_max =
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    if (max_package_bytes == 0u || max_package_bytes > java_long_max ||
        max_payload_bytes == 0u || max_payload_bytes > java_long_max ||
        max_zip_entries == 0u || max_zip_entries > java_int_max ||
        max_cumulative_inflated_bytes == 0u ||
        max_cumulative_inflated_bytes > java_long_max ||
        max_name_bytes == 0u || max_name_bytes > 0xFFFFu ||
        max_compression_ratio == 0u || max_compression_ratio > java_int_max ||
        ratio_guard_threshold_bytes > java_long_max)
    {
        return std::nullopt;
    }
    return BoundedZipLimits(
        max_package_bytes,
        max_payload_bytes,
        static_cast<std::uint32_t>(max_zip_entries),
        max_cumulative_inflated_bytes,
        static_cast<std::uint16_t>(max_name_bytes),
        static_cast<std::uint32_t>(max_compression_ratio),
        ratio_guard_threshold_bytes);
}

BoundedZipLimits BoundedZipLimits::defaults() noexcept
{
    return BoundedZipLimits(
        8u * 1024u * 1024u,
        8u * 1024u * 1024u,
        2048u,
        32u * 1024u * 1024u,
        1024u,
        200u,
        1024u * 1024u);
}

std::uint64_t BoundedZipLimits::max_package_bytes() const noexcept
{
    return max_package_bytes_;
}

std::uint64_t BoundedZipLimits::max_payload_bytes() const noexcept
{
    return max_payload_bytes_;
}

std::uint32_t BoundedZipLimits::max_zip_entries() const noexcept
{
    return max_zip_entries_;
}

std::uint64_t BoundedZipLimits::max_cumulative_inflated_bytes() const noexcept
{
    return max_cumulative_inflated_bytes_;
}

std::uint16_t BoundedZipLimits::max_name_bytes() const noexcept
{
    return max_name_bytes_;
}

std::uint32_t BoundedZipLimits::max_compression_ratio() const noexcept
{
    return max_compression_ratio_;
}

std::uint64_t BoundedZipLimits::ratio_guard_threshold_bytes() const noexcept
{
    return ratio_guard_threshold_bytes_;
}

BoundedZipArchive::BoundedZipArchive(BoundedZipLimits limits,
                                     std::vector<std::uint8_t> physical_bytes,
                                     std::vector<BoundedZipEntry> entries)
    : limits_(limits),
      physical_bytes_(std::move(physical_bytes)),
      entries_(std::move(entries))
{
}

std::size_t BoundedZipArchive::physical_size() const noexcept
{
    return physical_bytes_.size();
}

const std::vector<std::uint8_t>& BoundedZipArchive::physical_bytes() const noexcept
{
    return physical_bytes_;
}

const BoundedZipLimits& BoundedZipArchive::limits() const noexcept
{
    return limits_;
}

const std::vector<BoundedZipEntry>& BoundedZipArchive::entries() const noexcept
{
    return entries_;
}

BoundedZipOpenResult::BoundedZipOpenResult(
    std::optional<BoundedZipArchive> archive,
    std::optional<BoundedZipOpenError> error)
    : archive_(std::move(archive)), error_(std::move(error))
{
}

BoundedZipOpenResult BoundedZipOpenResult::success(BoundedZipArchive archive)
{
    return BoundedZipOpenResult(std::move(archive), std::nullopt);
}

BoundedZipOpenResult BoundedZipOpenResult::failure(ZipOpenCode code, std::string message)
{
    return BoundedZipOpenResult(
        std::nullopt, BoundedZipOpenError{code, std::move(message)});
}

bool BoundedZipOpenResult::succeeded() const noexcept
{
    return archive_.has_value();
}

const BoundedZipArchive* BoundedZipOpenResult::archive() const noexcept
{
    return archive_.has_value() ? &*archive_ : nullptr;
}

const BoundedZipOpenError* BoundedZipOpenResult::error() const noexcept
{
    return error_.has_value() ? &*error_ : nullptr;
}

BoundedZipOpenResult open_bounded_zip(const std::uint8_t* bytes,
                                      std::size_t size,
                                      const BoundedZipLimits& limits)
{
    if (static_cast<std::uint64_t>(size) > limits.max_package_bytes())
    {
        return BoundedZipOpenResult::failure(
            ZipOpenCode::PACKAGE_LIMIT_EXCEEDED,
            "ZIP package is over the source limit");
    }
    if (bytes == nullptr && size != 0u)
    {
        return BoundedZipOpenResult::failure(
            ZipOpenCode::INVALID_ZIP,
            "ZIP bytes pointer is null");
    }

    std::vector<std::uint8_t> physical_bytes;
    if (size != 0u)
    {
        physical_bytes.assign(bytes, bytes + size);
    }
    try
    {
        std::vector<BoundedZipEntry> entries = parse_entries(physical_bytes, limits);
        return BoundedZipOpenResult::success(
            BoundedZipArchive(limits, std::move(physical_bytes), std::move(entries)));
    }
    catch (const ValidationFailure& failure)
    {
        return BoundedZipOpenResult::failure(failure.code(), failure.what());
    }
}

} // namespace flynes::catalog
