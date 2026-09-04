#ifndef FLYNES_CATALOG_BOUNDED_ZIP_ARCHIVE_HPP
#define FLYNES_CATALOG_BOUNDED_ZIP_ARCHIVE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flynes::catalog {

/** Stable internal equivalents of the Java BoundedZipArchive.Code values. */
enum class ZipOpenCode : std::uint8_t
{
    INVALID_ZIP = 0,
    PACKAGE_LIMIT_EXCEEDED = 1,
    ENTRY_LIMIT_EXCEEDED = 2,
    INFLATED_LIMIT_EXCEEDED = 3,
    PAYLOAD_LIMIT_EXCEEDED = 4,
    NAME_LIMIT_EXCEEDED = 5,
    RATIO_LIMIT_EXCEEDED = 6,
    ENCRYPTED = 7,
    UNSUPPORTED_COMPRESSION = 8,
    ENTRY_MISSING = 9,
};

const char* zip_open_code_name(ZipOpenCode code) noexcept;

/**
 * Limits validated with the same invariants as Java ScanLimits.
 * Every archive bound rejects only when the actual value is greater than the limit.
 */
class BoundedZipLimits final
{
public:
    static std::optional<BoundedZipLimits> create(
        std::uint64_t max_package_bytes,
        std::uint64_t max_payload_bytes,
        std::uint64_t max_zip_entries,
        std::uint64_t max_cumulative_inflated_bytes,
        std::uint64_t max_name_bytes,
        std::uint64_t max_compression_ratio,
        std::uint64_t ratio_guard_threshold_bytes) noexcept;

    static BoundedZipLimits defaults() noexcept;

    std::uint64_t max_package_bytes() const noexcept;
    std::uint64_t max_payload_bytes() const noexcept;
    std::uint32_t max_zip_entries() const noexcept;
    std::uint64_t max_cumulative_inflated_bytes() const noexcept;
    std::uint16_t max_name_bytes() const noexcept;
    std::uint32_t max_compression_ratio() const noexcept;
    std::uint64_t ratio_guard_threshold_bytes() const noexcept;

private:
    BoundedZipLimits(std::uint64_t max_package_bytes,
                     std::uint64_t max_payload_bytes,
                     std::uint32_t max_zip_entries,
                     std::uint64_t max_cumulative_inflated_bytes,
                     std::uint16_t max_name_bytes,
                     std::uint32_t max_compression_ratio,
                     std::uint64_t ratio_guard_threshold_bytes) noexcept;

    std::uint64_t max_package_bytes_;
    std::uint64_t max_payload_bytes_;
    std::uint32_t max_zip_entries_;
    std::uint64_t max_cumulative_inflated_bytes_;
    std::uint16_t max_name_bytes_;
    std::uint32_t max_compression_ratio_;
    std::uint64_t ratio_guard_threshold_bytes_;
};

/** Exposed open-time metadata. Raw name plus local offset is the exact entry identity. */
struct BoundedZipEntry final
{
    std::vector<std::uint8_t> raw_name;
    std::vector<std::uint8_t> central_extra;
    std::int32_t local_header_offset = 0;
    std::uint16_t flags = 0;
    std::uint16_t method = 0;
    std::uint32_t crc32 = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
    bool is_directory = false;
};

class BoundedZipOpenResult;

/**
 * Owns exactly one physical archive byte copy plus bounded metadata.
 * This slice intentionally exposes no payload read/inflate operation.
 */
class BoundedZipArchive final
{
public:
    BoundedZipArchive(const BoundedZipArchive&) = delete;
    BoundedZipArchive& operator=(const BoundedZipArchive&) = delete;
    BoundedZipArchive(BoundedZipArchive&&) noexcept = default;
    BoundedZipArchive& operator=(BoundedZipArchive&&) noexcept = default;

    std::size_t physical_size() const noexcept;
    const std::vector<std::uint8_t>& physical_bytes() const noexcept;
    const BoundedZipLimits& limits() const noexcept;
    const std::vector<BoundedZipEntry>& entries() const noexcept;

private:
    friend class BoundedZipOpenResult;
    friend BoundedZipOpenResult open_bounded_zip(
        const std::uint8_t*, std::size_t, const BoundedZipLimits&);

    BoundedZipArchive(BoundedZipLimits limits,
                      std::vector<std::uint8_t> physical_bytes,
                      std::vector<BoundedZipEntry> entries);

    BoundedZipLimits limits_;
    std::vector<std::uint8_t> physical_bytes_;
    std::vector<BoundedZipEntry> entries_;
};

struct BoundedZipOpenError final
{
    ZipOpenCode code = ZipOpenCode::INVALID_ZIP;
    std::string message;
};

/** A stable value result; malformed input is reported here rather than thrown. */
class BoundedZipOpenResult final
{
public:
    BoundedZipOpenResult(const BoundedZipOpenResult&) = delete;
    BoundedZipOpenResult& operator=(const BoundedZipOpenResult&) = delete;
    BoundedZipOpenResult(BoundedZipOpenResult&&) noexcept = default;
    BoundedZipOpenResult& operator=(BoundedZipOpenResult&&) noexcept = default;

    static BoundedZipOpenResult success(BoundedZipArchive archive);
    static BoundedZipOpenResult failure(ZipOpenCode code, std::string message);

    bool succeeded() const noexcept;
    const BoundedZipArchive* archive() const noexcept;
    const BoundedZipOpenError* error() const noexcept;

private:
    BoundedZipOpenResult(std::optional<BoundedZipArchive> archive,
                         std::optional<BoundedZipOpenError> error);

    std::optional<BoundedZipArchive> archive_;
    std::optional<BoundedZipOpenError> error_;
};

/**
 * Copies an accepted byte view once, then validates ZIP structure and declared metadata.
 * Allocation failures may propagate; malformed archive data never crosses this boundary as an
 * exception. A null pointer is accepted only when size is zero.
 */
BoundedZipOpenResult open_bounded_zip(const std::uint8_t* bytes,
                                      std::size_t size,
                                      const BoundedZipLimits& limits);

} // namespace flynes::catalog

#endif
