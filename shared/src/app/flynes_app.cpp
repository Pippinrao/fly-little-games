#include <flynes/flynes_app.h>
#include <flynes/catalog/game_title_index.hpp>

#include "app/catalog_persist.hpp"
#include "app/catalog_state.hpp"
#include "app/layout_persist.hpp"
#include "app/settings_persist.hpp"
#include "catalog/bounded_zip_archive.hpp"
#include "flynes/product/control_layout.hpp"
#include "catalog/content_identity.hpp"
#include "catalog/rom_payload_parser.hpp"
#include "catalog/unsupported_payload_classifier.hpp"
#include "gbk_table.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

using flynes::app::CatalogData;
using flynes::app::CatalogEntryData;
using flynes::app::SettingsData;
using flynes::app::SourceKey;
using flynes::app::SourceRecord;
using flynes::app::UserRecord;
using flynes::app::same_source;
using flynes::catalog::BoundedZipArchive;
using flynes::catalog::BoundedZipEntry;
using flynes::catalog::BoundedZipLimits;
using flynes::catalog::ByteView;
using flynes::catalog::CompatibilityReason;
using flynes::catalog::CompatibilityState;
using flynes::catalog::ContentBytes;
using flynes::catalog::RomFormat;
using flynes::catalog::RomParseResult;
using flynes::catalog::RomWarning;

struct AppState final
{
    std::mutex mutex;
    std::string data_root;
    std::shared_ptr<const CatalogData> catalog = std::make_shared<CatalogData>();
    SettingsData settings;
    std::string control_layout_utf8;
};

bool source_scope_conflicts(const CatalogData& catalog, const SourceKey& source) noexcept
{
    for (const SourceRecord& existing : catalog.sources)
    {
        if (existing.key.uuid == source.uuid && existing.key.scope != source.scope)
        {
            return true;
        }
    }
    return false;
}

void upsert_source(std::vector<SourceRecord>& sources,
                   const SourceKey& source,
                   std::uint32_t completeness)
{
    for (SourceRecord& existing : sources)
    {
        if (existing.key.uuid == source.uuid)
        {
            existing.key.scope = source.scope;
            existing.last_completeness = completeness;
            return;
        }
    }
    SourceRecord record;
    record.key = source;
    record.last_completeness = completeness;
    sources.push_back(std::move(record));
}

enum class CandidateDisposition : std::uint8_t
{
    INDEXED,
    SKIPPED,
    REJECTED,
};

struct CandidateRecord final
{
    std::string source_relative_path;
    CandidateDisposition disposition = CandidateDisposition::REJECTED;
    std::uint32_t reason = FLY_SCAN_FILE_REASON_IO_ERROR;
    std::vector<CatalogEntryData> entries;
};

bool is_utf8_continuation(std::uint8_t byte) noexcept
{
    return byte >= 0x80u && byte <= 0xBFu;
}

bool is_valid_utf8(const char* bytes,
                   std::uint32_t length,
                   std::uint32_t maximum_length) noexcept
{
    if (bytes == nullptr || length == 0u || length > maximum_length)
    {
        return false;
    }
    std::uint32_t index = 0u;
    while (index < length)
    {
        const auto first = static_cast<std::uint8_t>(bytes[index]);
        if (first <= 0x7Fu)
        {
            if (first == 0u)
            {
                return false;
            }
            ++index;
            continue;
        }
        if (first >= 0xC2u && first <= 0xDFu)
        {
            if (length - index < 2u ||
                !is_utf8_continuation(static_cast<std::uint8_t>(bytes[index + 1u])))
            {
                return false;
            }
            index += 2u;
            continue;
        }
        if (first >= 0xE0u && first <= 0xEFu)
        {
            if (length - index < 3u)
            {
                return false;
            }
            const auto second = static_cast<std::uint8_t>(bytes[index + 1u]);
            const auto third = static_cast<std::uint8_t>(bytes[index + 2u]);
            const bool valid_second =
                is_utf8_continuation(second) &&
                (first != 0xE0u || second >= 0xA0u) &&
                (first != 0xEDu || second <= 0x9Fu);
            if (!valid_second || !is_utf8_continuation(third))
            {
                return false;
            }
            index += 3u;
            continue;
        }
        if (first >= 0xF0u && first <= 0xF4u)
        {
            if (length - index < 4u)
            {
                return false;
            }
            const auto second = static_cast<std::uint8_t>(bytes[index + 1u]);
            const auto third = static_cast<std::uint8_t>(bytes[index + 2u]);
            const auto fourth = static_cast<std::uint8_t>(bytes[index + 3u]);
            const bool valid_second =
                is_utf8_continuation(second) &&
                (first != 0xF0u || second >= 0x90u) &&
                (first != 0xF4u || second <= 0x8Fu);
            if (!valid_second || !is_utf8_continuation(third) ||
                !is_utf8_continuation(fourth))
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

bool is_valid_root_utf8(const char* bytes, std::uint32_t length) noexcept
{
    return is_valid_utf8(bytes, length, FLY_APP_ROOT_MAX_UTF8_BYTES);
}

bool is_ascii_letter(char value) noexcept
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

bool is_safe_relative_path(std::string_view path) noexcept
{
    if (path.empty() || path.front() == '/' || path.front() == '\\' ||
        path.back() == '/' || path.back() == '\\' ||
        path.find('\\') != std::string_view::npos)
    {
        return false;
    }
    if (path.size() >= 3u && is_ascii_letter(path[0]) && path[1] == ':' && path[2] == '/')
    {
        return false;
    }
    std::size_t start = 0u;
    while (start < path.size())
    {
        const std::size_t separator = path.find('/', start);
        const std::size_t end =
            separator == std::string_view::npos ? path.size() : separator;
        const std::string_view segment = path.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..")
        {
            return false;
        }
        if (separator == std::string_view::npos)
        {
            break;
        }
        start = separator + 1u;
    }
    return true;
}

bool is_safe_display_name(std::string_view value) noexcept
{
    return !value.empty() && value != "." && value != ".." &&
           value.find('/') == std::string_view::npos &&
           value.find('\\') == std::string_view::npos;
}

bool is_valid_scope(std::uint32_t scope) noexcept
{
    return scope >= FLY_SOURCE_SCOPE_BUILTIN && scope <= FLY_SOURCE_SCOPE_MANAGED_LIBRARY;
}

bool is_valid_completeness(std::uint32_t completeness) noexcept
{
    return completeness >= FLY_SCAN_COMPLETENESS_FULL &&
           completeness <= FLY_SCAN_COMPLETENESS_FATAL;
}

bool is_zero_uuid(const std::uint8_t* uuid) noexcept
{
    for (std::size_t index = 0u; index < 16u; ++index)
    {
        if (uuid[index] != 0u)
        {
            return false;
        }
    }
    return true;
}

fly_result validate_capabilities(const fly_platform_capabilities* capabilities) noexcept
{
    if (capabilities == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (capabilities->struct_size < FLY_PLATFORM_CAPABILITIES_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (capabilities->version != FLY_PLATFORM_CAPABILITIES_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    return FLY_RESULT_OK;
}

fly_result validate_config(const fly_app_config* config) noexcept
{
    if (config == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (config->struct_size < FLY_APP_CONFIG_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (config->version != FLY_APP_CONFIG_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (!is_valid_root_utf8(config->data_root_utf8, config->data_root_utf8_length) ||
        !is_valid_root_utf8(config->cache_root_utf8, config->cache_root_utf8_length))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return validate_capabilities(config->platform_capabilities);
}

fly_result validate_scan_config(const fly_scan_config* config) noexcept
{
    if (config == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (config->struct_size < FLY_SCAN_CONFIG_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (config->version != FLY_SCAN_CONFIG_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (is_zero_uuid(config->source_uuid) || !is_valid_scope(config->source_scope))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return FLY_RESULT_OK;
}

fly_result validate_scan_file(const fly_scan_file* file) noexcept
{
    if (file == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (file->struct_size < FLY_SCAN_FILE_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (file->version != FLY_SCAN_FILE_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if ((file->flags & ~FLY_SCAN_FILE_FLAG_EXPECTED_PHYSICAL_SHA256) != 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (!is_valid_utf8(file->source_relative_path_utf8,
                       file->source_relative_path_utf8_length,
                       FLY_SCAN_RELATIVE_PATH_MAX_UTF8_BYTES) ||
        !is_valid_utf8(file->display_name_utf8,
                       file->display_name_utf8_length,
                       FLY_SCAN_DISPLAY_NAME_MAX_UTF8_BYTES))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    const std::string_view path(file->source_relative_path_utf8,
                                file->source_relative_path_utf8_length);
    const std::string_view display(file->display_name_utf8,
                                   file->display_name_utf8_length);
    if (!is_safe_relative_path(path) || !is_safe_display_name(display))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return FLY_RESULT_OK;
}

fly_result validate_scan_file_result(const fly_scan_file_result* result) noexcept
{
    if (result == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (result->struct_size < FLY_SCAN_FILE_RESULT_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (result->version != FLY_SCAN_FILE_RESULT_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    return FLY_RESULT_OK;
}

enum class DescriptorReadStatus : std::uint8_t
{
    OK,
    IO_ERROR,
    TOO_LARGE,
};

struct DescriptorReadResult final
{
    DescriptorReadStatus status = DescriptorReadStatus::IO_ERROR;
    std::vector<std::uint8_t> bytes;
};

#if defined(_WIN32)
class RestoreFdOffset final
{
public:
    explicit RestoreFdOffset(std::int32_t fd)
        : fd_(fd), original_(_lseeki64(fd, 0, SEEK_CUR))
    {
        if (original_ < 0 || _lseeki64(fd_, 0, SEEK_SET) < 0)
        {
            original_ = -1;
        }
    }
    RestoreFdOffset(const RestoreFdOffset&) = delete;
    RestoreFdOffset& operator=(const RestoreFdOffset&) = delete;
    ~RestoreFdOffset()
    {
        if (original_ >= 0)
        {
            static_cast<void>(_lseeki64(fd_, original_, SEEK_SET));
        }
    }
    bool ready() const noexcept { return original_ >= 0; }
    bool restore() noexcept
    {
        if (original_ < 0)
        {
            return false;
        }
        const bool ok = _lseeki64(fd_, original_, SEEK_SET) >= 0;
        original_ = -1;
        return ok;
    }

private:
    std::int32_t fd_;
    __int64 original_;
};
#endif

DescriptorReadResult read_descriptor_bounded(std::int32_t fd)
{
    DescriptorReadResult result;
    if (fd < 0)
    {
        return result;
    }
#if defined(_WIN32)
    RestoreFdOffset restore(fd);
    if (!restore.ready())
    {
        return result;
    }
#endif
    std::array<std::uint8_t, 8192> buffer{};
    std::uint64_t offset = 0u;
    for (;;)
    {
        const std::uint64_t room = FLY_SCAN_MAX_PACKAGE_BYTES + 1u - offset;
        const std::size_t requested = static_cast<std::size_t>(
            std::min<std::uint64_t>(buffer.size(), room));
#if defined(_WIN32)
        const int amount = _read(fd, buffer.data(), static_cast<unsigned int>(requested));
        if (amount < 0)
        {
            return result;
        }
#else
        ssize_t amount = -1;
        do
        {
            amount = pread(fd, buffer.data(), requested, static_cast<off_t>(offset));
        } while (amount < 0 && errno == EINTR);
        if (amount < 0)
        {
            return result;
        }
#endif
        if (amount == 0)
        {
#if defined(_WIN32)
            if (!restore.restore())
            {
                return result;
            }
#endif
            result.status = DescriptorReadStatus::OK;
            return result;
        }
        result.bytes.insert(result.bytes.end(), buffer.begin(), buffer.begin() + amount);
        offset += static_cast<std::uint64_t>(amount);
        if (offset > FLY_SCAN_MAX_PACKAGE_BYTES)
        {
            result.status = DescriptorReadStatus::TOO_LARGE;
            result.bytes.clear();
            return result;
        }
    }
}

char hex_digit(std::uint8_t value) noexcept
{
    static constexpr char digits[] = "0123456789ABCDEF";
    return digits[value & 0x0Fu];
}

std::string bytes_hex(const std::uint8_t* bytes, std::size_t size)
{
    std::string output(size * 2u, '\0');
    for (std::size_t index = 0u; index < size; ++index)
    {
        output[index * 2u] = hex_digit(static_cast<std::uint8_t>(bytes[index] >> 4u));
        output[index * 2u + 1u] = hex_digit(bytes[index]);
    }
    return output;
}

int hex_value(char value) noexcept
{
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

template <std::size_t Size>
bool decode_hex(std::string_view text, std::array<std::uint8_t, Size>& output) noexcept
{
    if (text.size() != Size * 2u)
    {
        return false;
    }
    for (std::size_t index = 0u; index < Size; ++index)
    {
        const int high = hex_value(text[index * 2u]);
        const int low = hex_value(text[index * 2u + 1u]);
        if (high < 0 || low < 0)
        {
            return false;
        }
        output[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

bool has_zip_signature(const std::vector<std::uint8_t>& bytes) noexcept
{
    if (bytes.size() < 4u || bytes[0] != 'P' || bytes[1] != 'K') return false;
    return (bytes[2] == 0x03u && bytes[3] == 0x04u) ||
           (bytes[2] == 0x05u && bytes[3] == 0x06u) ||
           (bytes[2] == 0x07u && bytes[3] == 0x08u);
}

char ascii_lower(char value) noexcept
{
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

bool ends_with_ascii_case_insensitive(std::string_view value, std::string_view suffix) noexcept
{
    if (value.size() < suffix.size()) return false;
    const std::size_t start = value.size() - suffix.size();
    for (std::size_t index = 0u; index < suffix.size(); ++index)
    {
        if (ascii_lower(value[start + index]) != ascii_lower(suffix[index])) return false;
    }
    return true;
}

bool is_executable_name(std::string_view name) noexcept
{
    static constexpr std::array<std::string_view, 8> suffixes = {
        ".exe", ".dll", ".so", ".dylib", ".apk", ".hap", ".ipa", ".wasm"};
    for (const std::string_view suffix : suffixes)
    {
        if (ends_with_ascii_case_insensitive(name, suffix)) return true;
    }
    return false;
}

std::uint32_t abi_rom_format(RomFormat format) noexcept
{
    switch (format)
    {
    case RomFormat::INES: return FLY_ROM_FORMAT_INES;
    case RomFormat::NES2: return FLY_ROM_FORMAT_NES2;
    case RomFormat::FDS: return FLY_ROM_FORMAT_FDS;
    case RomFormat::UNIF: return FLY_ROM_FORMAT_UNIF;
    case RomFormat::UNKNOWN: return 0u;
    }
    return 0u;
}

std::uint32_t abi_compatibility_state(CompatibilityState state) noexcept
{
    switch (state)
    {
    case CompatibilityState::PLAYABLE: return FLY_COMPATIBILITY_PLAYABLE;
    case CompatibilityState::UNSUPPORTED: return FLY_COMPATIBILITY_UNSUPPORTED;
    case CompatibilityState::INVALID: return FLY_COMPATIBILITY_INVALID;
    case CompatibilityState::UNKNOWN: return 0u;
    }
    return 0u;
}

std::uint32_t abi_compatibility_reason(CompatibilityReason reason) noexcept
{
    switch (reason)
    {
    case CompatibilityReason::PLAYABLE_NES:
        return FLY_COMPATIBILITY_REASON_PLAYABLE_NES;
    case CompatibilityReason::FDS_BIOS_API_NOT_IMPLEMENTED:
        return FLY_COMPATIBILITY_REASON_FDS_BIOS_API_NOT_IMPLEMENTED;
    case CompatibilityReason::UNIF_PRODUCT_DISABLED:
        return FLY_COMPATIBILITY_REASON_UNIF_PRODUCT_DISABLED;
    case CompatibilityReason::NES_HEADER_INVALID:
        return FLY_COMPATIBILITY_REASON_NES_HEADER_INVALID;
    case CompatibilityReason::NES_ZERO_PRG:
        return FLY_COMPATIBILITY_REASON_NES_ZERO_PRG;
    case CompatibilityReason::NES_TRUNCATED:
        return FLY_COMPATIBILITY_REASON_NES_TRUNCATED;
    case CompatibilityReason::NES_SIZE_OVERFLOW:
        return FLY_COMPATIBILITY_REASON_NES_SIZE_OVERFLOW;
    case CompatibilityReason::FDS_INVALID_HEADER:
        return FLY_COMPATIBILITY_REASON_FDS_INVALID_HEADER;
    case CompatibilityReason::FDS_INVALID_SIDE_COUNT:
        return FLY_COMPATIBILITY_REASON_FDS_INVALID_SIDE_COUNT;
    case CompatibilityReason::FDS_TRUNCATED:
        return FLY_COMPATIBILITY_REASON_FDS_TRUNCATED;
    case CompatibilityReason::UNIF_INVALID_CHUNK:
        return FLY_COMPATIBILITY_REASON_UNIF_INVALID_CHUNK;
    case CompatibilityReason::UNIF_MISSING_PRG:
        return FLY_COMPATIBILITY_REASON_UNIF_MISSING_PRG;
    case CompatibilityReason::UNKNOWN_FORMAT: return 0u;
    }
    return 0u;
}

std::uint32_t abi_entry_flags(const RomParseResult& parsed, bool is_zip) noexcept
{
    std::uint32_t flags = is_zip ? FLY_CATALOG_ENTRY_FLAG_ZIP_ENTRY : 0u;
    if (parsed.analysis.battery) flags |= FLY_CATALOG_ENTRY_FLAG_BATTERY;
    if (parsed.analysis.trainer) flags |= FLY_CATALOG_ENTRY_FLAG_TRAINER;
    for (std::size_t index = 0u; index < parsed.analysis.warning_count; ++index)
    {
        if (parsed.analysis.warnings[index] == RomWarning::TRAILING_DATA)
            flags |= FLY_CATALOG_ENTRY_FLAG_TRAILING_DATA;
        else if (parsed.analysis.warnings[index] == RomWarning::DIRTY_HEADER)
            flags |= FLY_CATALOG_ENTRY_FLAG_DIRTY_HEADER;
    }
    return flags;
}

std::string source_identity(const SourceKey& source)
{
    return "source:" + std::to_string(source.scope) + ":" +
           bytes_hex(source.uuid.data(), source.uuid.size());
}

CatalogEntryData make_catalog_entry(const SourceKey& source,
                                    std::string_view source_relative_path,
                                    std::string_view display_name,
                                    const std::vector<std::uint8_t>& payload,
                                    const std::vector<std::uint8_t>& physical,
                                    std::string_view physical_sha256,
                                    const RomParseResult& parsed,
                                    std::uint32_t package_format,
                                    const std::vector<std::uint8_t>* zip_raw_name,
                                    std::int32_t zip_local_header_offset)
{
    const auto hashes = flynes::catalog::hash_rom_content(
        ContentBytes{payload.data(), payload.size()},
        ContentBytes{physical.data(), physical.size()});
    if (!hashes.ok() || hashes.value.physical_package_sha256 != physical_sha256)
        throw std::runtime_error("catalog content hash invariant failed");
    const auto package = flynes::catalog::package_id(
        source_identity(source), source_relative_path);
    const auto canonical = flynes::catalog::provisional_game_id(hashes.value.payload_sha256);
    std::string locator(flynes::catalog::RAW_LOCATOR);
    if (zip_raw_name != nullptr)
    {
        locator = bytes_hex(zip_raw_name->data(), zip_raw_name->size()) + "@" +
                  std::to_string(zip_local_header_offset);
    }
    if (!package.ok() || !canonical.ok())
        throw std::runtime_error("catalog stable ID invariant failed");
    const auto variant = flynes::catalog::variant_id(
        package.value, locator, hashes.value.payload_sha256);
    if (!variant.ok()) throw std::runtime_error("catalog variant ID invariant failed");

    CatalogEntryData entry;
    entry.source = source;
    entry.payload_size = static_cast<std::uint64_t>(payload.size());
    entry.physical_size = static_cast<std::uint64_t>(physical.size());
    entry.expected_bytes = parsed.analysis.expected_bytes;
    entry.prg_bytes = parsed.analysis.prg_bytes;
    entry.chr_bytes = parsed.analysis.chr_bytes;
    entry.mapper = parsed.analysis.mapper;
    entry.submapper = parsed.analysis.submapper;
    entry.disk_sides = static_cast<std::uint32_t>(parsed.analysis.disk_sides);
    if (!decode_hex(hashes.value.payload_sha1, entry.payload_sha1) ||
        !decode_hex(hashes.value.payload_sha256, entry.payload_sha256) ||
        !decode_hex(hashes.value.physical_package_sha256, entry.physical_sha256) ||
        !decode_hex(hashes.value.crc32, entry.payload_crc32))
        throw std::runtime_error("catalog binary hash conversion failed");
    entry.package_format = package_format;
    entry.rom_format = abi_rom_format(parsed.format);
    entry.compatibility_state = abi_compatibility_state(parsed.state);
    entry.compatibility_reason = abi_compatibility_reason(parsed.reason);
    entry.flags = abi_entry_flags(parsed, zip_raw_name != nullptr);
    entry.canonical_id = canonical.value;
    entry.variant_id = variant.value;
    entry.display_name.assign(display_name.data(), display_name.size());
    entry.source_relative_path.assign(source_relative_path.data(), source_relative_path.size());
    entry.package_id = package.value;
    if (zip_raw_name != nullptr)
    {
        entry.zip_raw_name = *zip_raw_name;
        entry.zip_local_header_offset = zip_local_header_offset;
    }
    return entry;
}

CandidateRecord rejected_candidate(std::string path, std::uint32_t reason)
{
    CandidateRecord result;
    result.source_relative_path = std::move(path);
    result.disposition = CandidateDisposition::REJECTED;
    result.reason = reason;
    return result;
}

CandidateRecord skipped_candidate(std::string path)
{
    CandidateRecord result;
    result.source_relative_path = std::move(path);
    result.disposition = CandidateDisposition::SKIPPED;
    result.reason = FLY_SCAN_FILE_REASON_NO_CATALOGABLE_ROM;
    return result;
}

bool expected_hash_matches(const fly_scan_file& file,
                           const std::string& physical_sha256) noexcept
{
    if ((file.flags & FLY_SCAN_FILE_FLAG_EXPECTED_PHYSICAL_SHA256) == 0u) return true;
    std::array<std::uint8_t, 32> actual{};
    return decode_hex(physical_sha256, actual) &&
           std::equal(actual.begin(), actual.end(), file.expected_physical_sha256);
}

CandidateRecord scan_raw_candidate(const SourceKey& source,
                                   std::string path,
                                   std::string display,
                                   std::vector<std::uint8_t> physical,
                                   const std::string& physical_sha256)
{
    if (is_executable_name(path) || is_executable_name(display))
        return skipped_candidate(std::move(path));
    const RomParseResult parsed = flynes::catalog::parse_rom_payload(
        ByteView{physical.data(), physical.size()});
    if (!parsed.recognized)
    {
        static_cast<void>(flynes::catalog::classify_unsupported_payload(
            ByteView{physical.data(), physical.size()}));
        return skipped_candidate(std::move(path));
    }
    CandidateRecord result;
    result.source_relative_path = path;
    result.disposition = CandidateDisposition::INDEXED;
    result.reason = FLY_SCAN_FILE_REASON_INDEXED;
    result.entries.push_back(make_catalog_entry(source,
                                                path,
                                                display,
                                                physical,
                                                physical,
                                                physical_sha256,
                                                parsed,
                                                FLY_PACKAGE_FORMAT_RAW,
                                                nullptr,
                                                -1));
    return result;
}

/**
 * Android RomPackageScanner GB18030-fallback parity: converts one GB18030
 * (GBK-compatible) byte string to UTF-8. Returns false when the input is not
 * fully decodable; control characters other than tab are rejected.
 */
bool gb18030_to_utf8(const std::vector<std::uint8_t>& input, std::string& utf8_out)
{
    // Decode strictly per GBK two-byte sequences (ASCII passes through) using
    // the generated delta table in gbk_table.h.
    std::u32string code_points;
    code_points.reserve(input.size());
    std::size_t index = 0u;
    while (index < input.size())
    {
        const std::uint8_t byte0 = input[index];
        if (byte0 < 0x80u)
        {
            if (byte0 == 0x00u || (byte0 < 0x20u && byte0 != 0x09u)) return false;
            code_points.push_back(static_cast<char32_t>(byte0));
            index += 1u;
            continue;
        }
        if (byte0 >= flynes::app::kGbkLeadMin && byte0 <= flynes::app::kGbkLeadMax &&
            index + 1u < input.size())
        {
            const std::uint8_t byte1 = input[index + 1u];
            if ((byte1 >= 0x40u && byte1 <= 0x7Eu) || (byte1 >= 0x80u && byte1 <= 0xFEu))
            {
                const std::uint16_t trail_index = byte1 < 0x7Fu
                    ? static_cast<std::uint16_t>(byte1 - 0x40u)
                    : static_cast<std::uint16_t>(byte1 - 0x80u + 63u);
                const std::uint16_t lead_index = byte0 - flynes::app::kGbkLeadMin;
                const std::uint32_t run_begin = flynes::app::kGbkLeadOffset[lead_index];
                const std::uint32_t run_end = flynes::app::kGbkLeadEnd[lead_index];
                bool mapped = false;
                for (std::uint32_t run = run_begin; run < run_end; ++run)
                {
                    const std::int32_t start = flynes::app::kGbkRuns[run][0];
                    const std::int32_t end = flynes::app::kGbkRuns[run][1];
                    if (static_cast<std::int32_t>(trail_index) < start ||
                        static_cast<std::int32_t>(trail_index) > end)
                    {
                        continue;
                    }
                    const std::uint32_t gbk_code =
                        (static_cast<std::uint32_t>(byte0) << 8u) | byte1;
                    const std::int32_t delta = flynes::app::kGbkRuns[run][2];
                    code_points.push_back(
                        static_cast<char32_t>(static_cast<std::int32_t>(gbk_code) + delta));
                    mapped = true;
                    break;
                }
                if (!mapped) return false;
                index += 2u;
                continue;
            }
            return false;
        }
        return false;
    }
    // Encode as UTF-8.
    utf8_out.clear();
    utf8_out.reserve(code_points.size() * 3u);
    for (const char32_t cp : code_points)
    {
        if (cp < 0x80u)
        {
            utf8_out.push_back(static_cast<char>(cp));
        }
        else if (cp < 0x800u)
        {
            utf8_out.push_back(static_cast<char>(0xC0u | (cp >> 6u)));
            utf8_out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
        else if (cp < 0x10000u)
        {
            utf8_out.push_back(static_cast<char>(0xE0u | (cp >> 12u)));
            utf8_out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            utf8_out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
        else
        {
            utf8_out.push_back(static_cast<char>(0xF0u | (cp >> 18u)));
            utf8_out.push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu)));
            utf8_out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            utf8_out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
    return true;
}

bool strict_zip_display_path(const BoundedZipEntry& zip_entry, std::string& decoded)
{
    if (zip_entry.raw_name.empty() || zip_entry.raw_name.size() > FLY_SCAN_MAX_ZIP_NAME_BYTES)
        return false;
    const char* bytes = reinterpret_cast<const char*>(zip_entry.raw_name.data());
    if (is_valid_utf8(bytes,
                      static_cast<std::uint32_t>(zip_entry.raw_name.size()),
                      FLY_SCAN_MAX_ZIP_NAME_BYTES))
    {
        decoded.assign(bytes, zip_entry.raw_name.size());
        return is_safe_relative_path(decoded);
    }
    // Android RomPackageScanner.decodeEntryName parity: non-UTF-8 names fall back
    // to GB18030 (most Chinese NES packs label entries in GBK). Re-encode the
    // decoded text as UTF-8 so display names carry real Chinese titles.
    if (gb18030_to_utf8(zip_entry.raw_name, decoded) &&
        decoded.size() <= FLY_SCAN_MAX_ZIP_NAME_BYTES &&
        is_valid_utf8(decoded.data(),
                      static_cast<std::uint32_t>(decoded.size()),
                      FLY_SCAN_MAX_ZIP_NAME_BYTES))
    {
        return is_safe_relative_path(decoded);
    }
    return false;
}

CandidateRecord scan_zip_candidate(const SourceKey& source,
                                   std::string path,
                                   const std::vector<std::uint8_t>& physical,
                                   const std::string& physical_sha256)
{
    auto opened = flynes::catalog::open_bounded_zip(
        physical.data(), physical.size(), BoundedZipLimits::defaults());
    if (!opened.succeeded())
        return rejected_candidate(std::move(path), FLY_SCAN_FILE_REASON_INVALID_ZIP);
    const BoundedZipArchive* archive = opened.archive();
    if (archive == nullptr) throw std::runtime_error("successful ZIP open has no archive");

    CandidateRecord result;
    result.source_relative_path = path;
    result.disposition = CandidateDisposition::SKIPPED;
    result.reason = FLY_SCAN_FILE_REASON_NO_CATALOGABLE_ROM;
    bool payload_error = false;
    for (const BoundedZipEntry& zip_entry : archive->entries())
    {
        if (zip_entry.is_directory) continue;
        std::string decoded_path;
        if (!strict_zip_display_path(zip_entry, decoded_path) ||
            is_executable_name(decoded_path))
            continue;
        if (zip_entry.uncompressed_size > FLY_SCAN_MAX_PAYLOAD_BYTES)
        {
            payload_error = true;
            continue;
        }
        auto payload_result = flynes::catalog::read_bounded_zip_payload(
            *archive, zip_entry.raw_name, zip_entry.local_header_offset);
        if (!payload_result.succeeded() || payload_result.payload() == nullptr)
        {
            payload_error = true;
            continue;
        }
        const std::vector<std::uint8_t>& payload = *payload_result.payload();
        const RomParseResult parsed = flynes::catalog::parse_rom_payload(
            ByteView{payload.data(), payload.size()});
        if (!parsed.recognized)
        {
            static_cast<void>(flynes::catalog::classify_unsupported_payload(
                ByteView{payload.data(), payload.size()}));
            continue;
        }
        result.entries.push_back(make_catalog_entry(source,
                                                    path,
                                                    decoded_path,
                                                    payload,
                                                    physical,
                                                    physical_sha256,
                                                    parsed,
                                                    FLY_PACKAGE_FORMAT_ZIP,
                                                    &zip_entry.raw_name,
                                                    zip_entry.local_header_offset));
    }
    if (!result.entries.empty())
    {
        result.disposition = CandidateDisposition::INDEXED;
        result.reason = FLY_SCAN_FILE_REASON_INDEXED;
    }
    else if (payload_error)
    {
        result.disposition = CandidateDisposition::REJECTED;
        result.reason = FLY_SCAN_FILE_REASON_INVALID_ZIP;
    }
    return result;
}

CandidateRecord scan_candidate(const SourceKey& source, const fly_scan_file& file)
{
    std::string path(file.source_relative_path_utf8, file.source_relative_path_utf8_length);
    std::string display(file.display_name_utf8, file.display_name_utf8_length);
    DescriptorReadResult read = read_descriptor_bounded(file.borrowed_fd);
    if (read.status == DescriptorReadStatus::IO_ERROR)
        return rejected_candidate(std::move(path), FLY_SCAN_FILE_REASON_IO_ERROR);
    if (read.status == DescriptorReadStatus::TOO_LARGE)
        return rejected_candidate(std::move(path), FLY_SCAN_FILE_REASON_PACKAGE_LIMIT_EXCEEDED);
    const auto physical_hash = flynes::catalog::sha256_hex(
        ContentBytes{read.bytes.data(), read.bytes.size()});
    if (!physical_hash.ok()) throw std::runtime_error("physical SHA-256 failed");
    if (!expected_hash_matches(file, physical_hash.value))
        return rejected_candidate(std::move(path), FLY_SCAN_FILE_REASON_HASH_MISMATCH);
    if (has_zip_signature(read.bytes))
        return scan_zip_candidate(source, std::move(path), read.bytes, physical_hash.value);
    return scan_raw_candidate(source,
                              std::move(path),
                              std::move(display),
                              std::move(read.bytes),
                              physical_hash.value);
}

bool entry_order(const CatalogEntryData& left, const CatalogEntryData& right) noexcept
{
    if (left.variant_id != right.variant_id) return left.variant_id < right.variant_id;
    if (left.source.uuid != right.source.uuid) return left.source.uuid < right.source.uuid;
    if (left.source.scope != right.source.scope) return left.source.scope < right.source.scope;
    if (left.source_relative_path != right.source_relative_path)
        return left.source_relative_path < right.source_relative_path;
    return left.display_name < right.display_name;
}

bool candidate_is_rejected(const std::vector<CandidateRecord>& candidates,
                           std::string_view path) noexcept
{
    for (const CandidateRecord& candidate : candidates)
    {
        if (candidate.source_relative_path == path)
            return candidate.disposition == CandidateDisposition::REJECTED;
    }
    return false;
}

void erase_source_path(std::vector<CatalogEntryData>& entries,
                       const SourceKey& source,
                       std::string_view path)
{
    entries.erase(
        std::remove_if(entries.begin(), entries.end(), [&](const CatalogEntryData& entry) {
            return same_source(entry.source, source) && entry.source_relative_path == path;
        }),
        entries.end());
}

std::shared_ptr<const CatalogData> reconcile_catalog(
    const CatalogData& current,
    const SourceKey& source,
    std::uint32_t completeness,
    const std::vector<CandidateRecord>& candidates)
{
    if (current.generation == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("catalog generation exhausted");
    auto next = std::make_shared<CatalogData>();
    next->generation = current.generation + 1u;
    next->sources = current.sources;
    next->users = current.users;
    next->next_favorite_revision = current.next_favorite_revision;
    next->next_play_sequence = current.next_play_sequence;
    upsert_source(next->sources, source, completeness);
    next->entries.reserve(current.entries.size());
    for (const CatalogEntryData& existing : current.entries)
    {
        if (!same_source(existing.source, source))
        {
            next->entries.push_back(existing);
            continue;
        }
        const bool preserve =
            completeness == FLY_SCAN_COMPLETENESS_PARTIAL ||
            completeness == FLY_SCAN_COMPLETENESS_FATAL ||
            candidate_is_rejected(candidates, existing.source_relative_path);
        if (preserve)
        {
            CatalogEntryData stale = existing;
            stale.freshness = FLY_CATALOG_FRESHNESS_STALE;
            next->entries.push_back(std::move(stale));
        }
    }
    if (completeness != FLY_SCAN_COMPLETENESS_FATAL)
    {
        for (const CandidateRecord& candidate : candidates)
        {
            if (candidate.disposition == CandidateDisposition::REJECTED) continue;
            erase_source_path(next->entries, source, candidate.source_relative_path);
            if (candidate.disposition == CandidateDisposition::INDEXED)
            {
                for (const CatalogEntryData& entry : candidate.entries)
                {
                    CatalogEntryData fresh = entry;
                    fresh.freshness = FLY_CATALOG_FRESHNESS_FRESH;
                    next->entries.push_back(std::move(fresh));
                }
            }
        }
    }
    std::sort(next->entries.begin(), next->entries.end(), entry_order);
    return next;
}

bool has_candidate_path(const std::vector<CandidateRecord>& candidates,
                        std::string_view path) noexcept
{
    for (const CandidateRecord& candidate : candidates)
    {
        if (candidate.source_relative_path == path) return true;
    }
    return false;
}

bool valid_output_buffer(char* buffer, std::uint32_t capacity) noexcept
{
    return buffer != nullptr || capacity == 0u;
}

std::uint32_t required_string_size(const std::string& value) noexcept
{
    return static_cast<std::uint32_t>(value.size() + 1u);
}

void copy_string(char* destination, const std::string& value) noexcept
{
    std::memcpy(destination, value.data(), value.size());
    destination[value.size()] = '\0';
}

fly_result validate_canonical_id(const char* bytes, std::uint32_t length) noexcept
{
    if (!is_valid_utf8(bytes, length, FLY_CANONICAL_ID_MAX_UTF8_BYTES))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return FLY_RESULT_OK;
}

const UserRecord* find_user(const CatalogData& catalog, std::string_view canonical_id) noexcept
{
    for (const UserRecord& user : catalog.users)
    {
        if (user.canonical_id == canonical_id)
        {
            return &user;
        }
    }
    return nullptr;
}

UserRecord& upsert_user(CatalogData& catalog, std::string_view canonical_id)
{
    for (UserRecord& user : catalog.users)
    {
        if (user.canonical_id == canonical_id)
        {
            return user;
        }
    }
    UserRecord created;
    created.canonical_id.assign(canonical_id.data(), canonical_id.size());
    catalog.users.push_back(std::move(created));
    return catalog.users.back();
}

std::uint32_t source_freshness_summary(const CatalogData& catalog, const SourceKey& source) noexcept
{
    for (const CatalogEntryData& entry : catalog.entries)
    {
        if (same_source(entry.source, source) &&
            entry.freshness == FLY_CATALOG_FRESHNESS_STALE)
        {
            return FLY_CATALOG_FRESHNESS_STALE;
        }
    }
    return FLY_CATALOG_FRESHNESS_FRESH;
}

bool in_closed_range(float value, float minimum, float maximum) noexcept
{
    return !std::isnan(value) && value >= minimum && value <= maximum;
}

bool is_flag(std::uint32_t value) noexcept
{
    return value == 0u || value == 1u;
}

fly_result validate_settings_snapshot(const fly_settings_snapshot& snapshot) noexcept
{
    if (snapshot.struct_size < FLY_SETTINGS_SNAPSHOT_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (snapshot.version != FLY_SETTINGS_SNAPSHOT_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (snapshot.aspect_mode < FLY_ASPECT_FOUR_BY_THREE ||
        snapshot.aspect_mode > FLY_ASPECT_INTEGER_SCALE ||
        snapshot.video_quality_preset < FLY_VIDEO_QUALITY_POWER_SAVER ||
        snapshot.video_quality_preset > FLY_VIDEO_QUALITY_CUSTOM ||
        snapshot.custom_refresh_policy < FLY_REFRESH_FOLLOW_SYSTEM ||
        snapshot.custom_refresh_policy > FLY_REFRESH_HZ_120 ||
        snapshot.custom_temporal_mode < FLY_TEMPORAL_NATIVE ||
        snapshot.custom_temporal_mode > FLY_TEMPORAL_MOTION_INTERPOLATION ||
        snapshot.custom_spatial_mode < FLY_SPATIAL_NEAREST ||
        snapshot.custom_spatial_mode > FLY_SPATIAL_SCALEFX ||
        snapshot.custom_post_effect < FLY_POST_EFFECT_NONE ||
        snapshot.custom_post_effect > FLY_POST_EFFECT_CRT ||
        snapshot.layout_preset < FLY_LAYOUT_STANDARD_BA ||
        snapshot.layout_preset > FLY_LAYOUT_MIRRORED_AB ||
        snapshot.direction_mode < FLY_DIRECTION_JOYSTICK ||
        snapshot.direction_mode > FLY_DIRECTION_DPAD ||
        snapshot.haptic_level < FLY_HAPTIC_OFF ||
        snapshot.haptic_level > FLY_HAPTIC_STRONG ||
        snapshot.audio_focus_policy < FLY_AUDIO_FOCUS_PAUSE ||
        snapshot.audio_focus_policy > FLY_AUDIO_FOCUS_IGNORE)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (!is_flag(snapshot.adaptive_protection) || !is_flag(snapshot.distinct_ab_haptics) ||
        !is_flag(snapshot.audio_enabled) || !is_flag(snapshot.autosave_enabled))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (!in_closed_range(snapshot.button_scale, FLY_SETTINGS_BUTTON_SCALE_MIN,
                         FLY_SETTINGS_BUTTON_SCALE_MAX) ||
        !in_closed_range(snapshot.vertical_offset, FLY_SETTINGS_VERTICAL_OFFSET_MIN,
                         FLY_SETTINGS_VERTICAL_OFFSET_MAX) ||
        !in_closed_range(snapshot.control_opacity, FLY_SETTINGS_CONTROL_OPACITY_MIN,
                         FLY_SETTINGS_CONTROL_OPACITY_MAX) ||
        !in_closed_range(snapshot.joystick_scale, FLY_SETTINGS_JOYSTICK_SCALE_MIN,
                         FLY_SETTINGS_JOYSTICK_SCALE_MAX) ||
        !in_closed_range(snapshot.dead_zone, FLY_SETTINGS_DEAD_ZONE_MIN,
                         FLY_SETTINGS_DEAD_ZONE_MAX))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (!is_valid_utf8(snapshot.locale_tag_utf8,
                       snapshot.locale_tag_utf8_length,
                       FLY_SETTINGS_LOCALE_MAX_UTF8_BYTES))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (snapshot.last_played_id_utf8_length != 0u &&
        !is_valid_utf8(snapshot.last_played_id_utf8,
                       snapshot.last_played_id_utf8_length,
                       FLY_CANONICAL_ID_MAX_UTF8_BYTES))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return FLY_RESULT_OK;
}

SettingsData settings_from_snapshot(const fly_settings_snapshot& snapshot)
{
    SettingsData settings;
    settings.aspect_mode = snapshot.aspect_mode;
    settings.video_quality_preset = snapshot.video_quality_preset;
    settings.custom_refresh_policy = snapshot.custom_refresh_policy;
    settings.custom_temporal_mode = snapshot.custom_temporal_mode;
    settings.custom_spatial_mode = snapshot.custom_spatial_mode;
    settings.custom_post_effect = snapshot.custom_post_effect;
    settings.adaptive_protection = snapshot.adaptive_protection;
    settings.layout_preset = snapshot.layout_preset;
    settings.direction_mode = snapshot.direction_mode;
    settings.button_scale = snapshot.button_scale;
    settings.vertical_offset = snapshot.vertical_offset;
    settings.control_opacity = snapshot.control_opacity;
    settings.joystick_scale = snapshot.joystick_scale;
    settings.dead_zone = snapshot.dead_zone;
    settings.haptic_level = snapshot.haptic_level;
    settings.distinct_ab_haptics = snapshot.distinct_ab_haptics;
    settings.audio_enabled = snapshot.audio_enabled;
    settings.audio_focus_policy = snapshot.audio_focus_policy;
    settings.autosave_enabled = snapshot.autosave_enabled;
    settings.locale_tag.assign(snapshot.locale_tag_utf8, snapshot.locale_tag_utf8_length);
    if (snapshot.last_played_id_utf8_length == 0u)
    {
        settings.last_played_id.clear();
    }
    else
    {
        settings.last_played_id.assign(snapshot.last_played_id_utf8,
                                       snapshot.last_played_id_utf8_length);
    }
    return settings;
}

} // namespace

struct fly_app_handle final
{
    fly_app_handle(const fly_app_config& config, std::shared_ptr<AppState> app_state)
        : data_root(config.data_root_utf8, config.data_root_utf8_length),
          cache_root(config.cache_root_utf8, config.cache_root_utf8_length),
          platform_flags(config.platform_capabilities->flags),
          state(std::move(app_state))
    {
    }
    std::string data_root;
    std::string cache_root;
    std::uint64_t platform_flags;
    std::shared_ptr<AppState> state;
};

struct fly_catalog_snapshot_handle final
{
    explicit fly_catalog_snapshot_handle(std::shared_ptr<const CatalogData> catalog_data)
        : catalog(std::move(catalog_data))
    {
    }
    std::shared_ptr<const CatalogData> catalog;
};

struct fly_scan_handle final
{
    fly_scan_handle(std::weak_ptr<AppState> app_state,
                    SourceKey source_key,
                    std::uint64_t generation)
        : app(std::move(app_state)),
          source(std::move(source_key)),
          base_generation(generation)
    {
    }
    std::mutex mutex;
    std::weak_ptr<AppState> app;
    SourceKey source;
    std::uint64_t base_generation;
    std::vector<CandidateRecord> candidates;
    bool closed = false;
    bool poisoned = false;
};

extern "C" fly_result fly_app_create(const fly_app_config* config, fly_app_t** app_out)
{
    if (app_out == nullptr) return FLY_RESULT_INVALID_ARGUMENT;
    *app_out = nullptr;
    const fly_result validation = validate_config(config);
    if (validation != FLY_RESULT_OK) return validation;
    try
    {
        auto state = std::make_shared<AppState>();
        state->data_root.assign(config->data_root_utf8, config->data_root_utf8_length);
        state->catalog = flynes::app::load_catalog(state->data_root);
        state->settings = flynes::app::load_settings(state->data_root);
        state->control_layout_utf8 = flynes::app::load_control_layout(state->data_root);
        auto app = std::make_unique<fly_app_t>(*config, std::move(state));
        *app_out = app.release();
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" void fly_app_destroy(fly_app_t* app)
{
    delete app;
}

extern "C" fly_result fly_scan_begin(fly_app_t* app,
                                      const fly_scan_config* config,
                                      fly_scan_t** scan_out)
{
    if (scan_out == nullptr) return FLY_RESULT_INVALID_ARGUMENT;
    *scan_out = nullptr;
    if (app == nullptr) return FLY_RESULT_INVALID_ARGUMENT;
    const fly_result validation = validate_scan_config(config);
    if (validation != FLY_RESULT_OK) return validation;
    try
    {
        SourceKey source;
        std::copy(config->source_uuid, config->source_uuid + 16u, source.uuid.begin());
        source.scope = config->source_scope;
        std::uint64_t generation = 0u;
        {
            std::lock_guard<std::mutex> lock(app->state->mutex);
            if (source_scope_conflicts(*app->state->catalog, source))
            {
                return FLY_RESULT_CONFLICT;
            }
            generation = app->state->catalog->generation;
        }
        auto scan = std::make_unique<fly_scan_t>(
            app->state, std::move(source), generation);
        *scan_out = scan.release();
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" fly_result fly_scan_add_file(fly_scan_t* scan,
                                         const fly_scan_file* file,
                                         fly_scan_file_result* result_out)
{
    if (scan == nullptr) return FLY_RESULT_INVALID_ARGUMENT;
    const fly_result file_validation = validate_scan_file(file);
    if (file_validation != FLY_RESULT_OK) return file_validation;
    const fly_result result_validation = validate_scan_file_result(result_out);
    if (result_validation != FLY_RESULT_OK) return result_validation;
    std::lock_guard<std::mutex> lock(scan->mutex);
    if (scan->closed || scan->poisoned)
        return FLY_RESULT_INVALID_STATE;
    if (scan->app.expired())
    {
        scan->closed = true;
        return FLY_RESULT_INVALID_STATE;
    }
    const std::string_view path(file->source_relative_path_utf8,
                                file->source_relative_path_utf8_length);
    if (has_candidate_path(scan->candidates, path)) return FLY_RESULT_INVALID_ARGUMENT;
    try
    {
        CandidateRecord candidate = scan_candidate(scan->source, *file);
        fly_scan_file_result result = *result_out;
        switch (candidate.disposition)
        {
        case CandidateDisposition::INDEXED:
            result.outcome = FLY_SCAN_FILE_OUTCOME_INDEXED;
            break;
        case CandidateDisposition::SKIPPED:
            result.outcome = FLY_SCAN_FILE_OUTCOME_SKIPPED;
            break;
        case CandidateDisposition::REJECTED:
            result.outcome = FLY_SCAN_FILE_OUTCOME_REJECTED;
            break;
        }
        result.reason = candidate.reason;
        result.variant_count = static_cast<std::uint32_t>(candidate.entries.size());
        result.reserved = 0u;
        scan->candidates.push_back(std::move(candidate));
        *result_out = result;
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&)
    {
        scan->poisoned = true;
        return FLY_RESULT_OUT_OF_MEMORY;
    }
    catch (...)
    {
        scan->poisoned = true;
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" fly_result fly_scan_commit(fly_scan_t* scan,
                                       std::uint32_t final_completeness)
{
    if (scan == nullptr) return FLY_RESULT_INVALID_ARGUMENT;
    if (!is_valid_completeness(final_completeness)) return FLY_RESULT_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> scan_lock(scan->mutex);
    if (scan->closed || scan->poisoned) return FLY_RESULT_INVALID_STATE;
    scan->closed = true;
    const std::shared_ptr<AppState> app = scan->app.lock();
    if (app == nullptr) return FLY_RESULT_INVALID_STATE;
    try
    {
        std::lock_guard<std::mutex> app_lock(app->mutex);
        if (app->catalog->generation != scan->base_generation) return FLY_RESULT_CONFLICT;
        std::shared_ptr<const CatalogData> next = reconcile_catalog(
            *app->catalog, scan->source, final_completeness, scan->candidates);
        if (!flynes::app::save_catalog(app->data_root, *next))
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        app->catalog = std::move(next);
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" void fly_scan_abort(fly_scan_t* scan)
{
    delete scan;
}

extern "C" fly_result fly_catalog_snapshot(const fly_app_t* app,
                                             fly_catalog_snapshot_t** snapshot_out)
{
    if (snapshot_out == nullptr) return FLY_RESULT_INVALID_ARGUMENT;
    *snapshot_out = nullptr;
    if (app == nullptr) return FLY_RESULT_INVALID_ARGUMENT;
    try
    {
        std::shared_ptr<const CatalogData> catalog;
        {
            std::lock_guard<std::mutex> lock(app->state->mutex);
            catalog = app->state->catalog;
        }
        auto snapshot = std::make_unique<fly_catalog_snapshot_t>(std::move(catalog));
        *snapshot_out = snapshot.release();
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" fly_result fly_catalog_snapshot_generation(
    const fly_catalog_snapshot_t* snapshot,
    std::uint64_t* generation_out)
{
    if (snapshot == nullptr || generation_out == nullptr) return FLY_RESULT_INVALID_ARGUMENT;
    *generation_out = snapshot->catalog->generation;
    return FLY_RESULT_OK;
}

extern "C" fly_result fly_catalog_snapshot_count(const fly_catalog_snapshot_t* snapshot,
                                                   std::uint64_t* count_out)
{
    if (snapshot == nullptr || count_out == nullptr) return FLY_RESULT_INVALID_ARGUMENT;
    *count_out = static_cast<std::uint64_t>(snapshot->catalog->entries.size());
    return FLY_RESULT_OK;
}

extern "C" fly_result fly_catalog_snapshot_get(const fly_catalog_snapshot_t* snapshot,
                                                 std::uint64_t index,
                                                 fly_catalog_entry* entry_out)
{
    if (snapshot == nullptr || entry_out == nullptr) return FLY_RESULT_INVALID_ARGUMENT;
    if (entry_out->struct_size < FLY_CATALOG_ENTRY_V1_SIZE)
        return FLY_RESULT_STRUCT_TOO_SMALL;
    if (entry_out->version != FLY_CATALOG_ENTRY_VERSION_1)
        return FLY_RESULT_UNSUPPORTED_VERSION;
    if (index >= snapshot->catalog->entries.size()) return FLY_RESULT_OUT_OF_RANGE;
    if (!valid_output_buffer(entry_out->canonical_id_utf8,
                             entry_out->canonical_id_capacity) ||
        !valid_output_buffer(entry_out->variant_id_utf8,
                             entry_out->variant_id_capacity) ||
        !valid_output_buffer(entry_out->display_name_utf8,
                             entry_out->display_name_capacity) ||
        !valid_output_buffer(entry_out->source_relative_path_utf8,
                             entry_out->source_relative_path_capacity))
        return FLY_RESULT_INVALID_ARGUMENT;
    try
    {
        const CatalogEntryData& entry = snapshot->catalog->entries[static_cast<std::size_t>(index)];
        const std::uint32_t canonical_required = required_string_size(entry.canonical_id);
        const std::uint32_t variant_required = required_string_size(entry.variant_id);
        const std::uint32_t display_required = required_string_size(entry.display_name);
        const std::uint32_t path_required = required_string_size(entry.source_relative_path);
        if (entry_out->canonical_id_capacity < canonical_required ||
            entry_out->variant_id_capacity < variant_required ||
            entry_out->display_name_capacity < display_required ||
            entry_out->source_relative_path_capacity < path_required)
        {
            entry_out->canonical_id_required = canonical_required;
            entry_out->variant_id_required = variant_required;
            entry_out->display_name_required = display_required;
            entry_out->source_relative_path_required = path_required;
            return FLY_RESULT_BUFFER_TOO_SMALL;
        }

        fly_catalog_entry output = *entry_out;
        std::copy(entry.source.uuid.begin(), entry.source.uuid.end(), output.source_uuid);
        output.payload_size = entry.payload_size;
        output.physical_size = entry.physical_size;
        output.expected_bytes = entry.expected_bytes;
        output.prg_bytes = entry.prg_bytes;
        output.chr_bytes = entry.chr_bytes;
        output.mapper = entry.mapper;
        output.submapper = entry.submapper;
        output.disk_sides = entry.disk_sides;
        std::copy(entry.payload_sha1.begin(), entry.payload_sha1.end(), output.payload_sha1);
        std::copy(entry.payload_sha256.begin(), entry.payload_sha256.end(), output.payload_sha256);
        std::copy(entry.physical_sha256.begin(), entry.physical_sha256.end(), output.physical_sha256);
        std::copy(entry.payload_crc32.begin(), entry.payload_crc32.end(), output.payload_crc32);
        output.source_scope = entry.source.scope;
        output.package_format = entry.package_format;
        output.rom_format = entry.rom_format;
        output.compatibility_state = entry.compatibility_state;
        output.compatibility_reason = entry.compatibility_reason;
        output.freshness = entry.freshness;
        output.flags = entry.flags;
        output.canonical_id_required = canonical_required;
        output.variant_id_required = variant_required;
        output.display_name_required = display_required;
        output.source_relative_path_required = path_required;

        copy_string(entry_out->canonical_id_utf8, entry.canonical_id);
        copy_string(entry_out->variant_id_utf8, entry.variant_id);
        copy_string(entry_out->display_name_utf8, entry.display_name);
        copy_string(entry_out->source_relative_path_utf8, entry.source_relative_path);
        *entry_out = output;
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" fly_result fly_game_title_resolve(const std::uint8_t* payload_sha256,
    const char* fallback_name_utf8, std::uint32_t fallback_name_utf8_length, fly_game_title* out)
{
    if (out == nullptr || (fallback_name_utf8 == nullptr && fallback_name_utf8_length != 0u))
        return FLY_RESULT_INVALID_ARGUMENT;
    try
    {
        const std::string_view fallback = fallback_name_utf8 == nullptr ? std::string_view{} :
            std::string_view(fallback_name_utf8, fallback_name_utf8_length);
        const auto match = flynes::catalog::game_title_index().lookup(payload_sha256, fallback);
        fly_game_title result{"", "", "", "", 0u};
        if (match.record != nullptr)
        {
            result = {match.record->index_id, match.record->title_en, match.record->title_zh_hans,
                      match.record->aliases, match.match_kind};
        }
        *out = result;
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" fly_result fly_catalog_snapshot_get_title(const fly_catalog_snapshot_t* snapshot,
    std::uint64_t index, fly_game_title* out)
{
    if (snapshot == nullptr || out == nullptr) return FLY_RESULT_INVALID_ARGUMENT;
    if (index >= snapshot->catalog->entries.size()) return FLY_RESULT_OUT_OF_RANGE;
    const CatalogEntryData& entry = snapshot->catalog->entries[static_cast<std::size_t>(index)];
    return fly_game_title_resolve(entry.payload_sha256.data(), entry.display_name.data(),
        static_cast<std::uint32_t>(entry.display_name.size()), out);
}

extern "C" fly_result fly_catalog_snapshot_get_zip_locator(
    const fly_catalog_snapshot_t* snapshot, std::uint64_t index,
    std::uint8_t* raw_name, std::uint32_t capacity,
    std::uint32_t* required, std::int32_t* offset)
{
    if (snapshot == nullptr || required == nullptr || offset == nullptr ||
        (capacity > 0u && raw_name == nullptr)) return FLY_RESULT_INVALID_ARGUMENT;
    if (index >= snapshot->catalog->entries.size()) return FLY_RESULT_OUT_OF_RANGE;
    const CatalogEntryData& entry = snapshot->catalog->entries[static_cast<std::size_t>(index)];
    const auto length = static_cast<std::uint32_t>(entry.zip_raw_name.size());
    *required = length;
    if (capacity < length) return FLY_RESULT_BUFFER_TOO_SMALL;
    if (length > 0u) std::copy(entry.zip_raw_name.begin(), entry.zip_raw_name.end(), raw_name);
    *offset = entry.zip_local_header_offset;
    return FLY_RESULT_OK;
}

extern "C" void fly_catalog_snapshot_release(fly_catalog_snapshot_t* snapshot)
{
    delete snapshot;
}

extern "C" fly_result fly_catalog_user_state_get(const fly_app_t* app,
                                                 const char* canonical_id_utf8,
                                                 std::uint32_t canonical_id_utf8_length,
                                                 fly_catalog_user_state* state_out)
{
    if (app == nullptr || state_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (state_out->struct_size < FLY_CATALOG_USER_STATE_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (state_out->version != FLY_CATALOG_USER_STATE_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    const fly_result id_validation = validate_canonical_id(canonical_id_utf8,
                                                           canonical_id_utf8_length);
    if (id_validation != FLY_RESULT_OK)
    {
        return id_validation;
    }
    try
    {
        const std::string_view canonical_id(canonical_id_utf8, canonical_id_utf8_length);
        std::lock_guard<std::mutex> lock(app->state->mutex);
        fly_catalog_user_state output = *state_out;
        output.favorite = 0u;
        output.play_count = 0u;
        output.favorite_revision = 0u;
        output.last_played_sequence = 0u;
        if (const UserRecord* user = find_user(*app->state->catalog, canonical_id))
        {
            output.favorite = user->favorite ? 1u : 0u;
            output.play_count = user->play_count;
            output.favorite_revision = user->favorite_revision;
            output.last_played_sequence = user->last_played_sequence;
        }
        *state_out = output;
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" fly_result fly_catalog_favorite_set(fly_app_t* app,
                                               const char* canonical_id_utf8,
                                               std::uint32_t canonical_id_utf8_length,
                                               std::uint32_t favorite)
{
    if (app == nullptr || (favorite != 0u && favorite != 1u))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    const fly_result id_validation = validate_canonical_id(canonical_id_utf8,
                                                           canonical_id_utf8_length);
    if (id_validation != FLY_RESULT_OK)
    {
        return id_validation;
    }
    try
    {
        const std::string_view canonical_id(canonical_id_utf8, canonical_id_utf8_length);
        std::lock_guard<std::mutex> lock(app->state->mutex);
        auto next = std::make_shared<CatalogData>(*app->state->catalog);
        if (next->next_favorite_revision == std::numeric_limits<std::uint64_t>::max())
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        UserRecord& user = upsert_user(*next, canonical_id);
        user.favorite = favorite == 1u;
        user.favorite_revision = ++next->next_favorite_revision;
        if (!flynes::app::save_catalog(app->state->data_root, *next))
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        app->state->catalog = std::move(next);
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" fly_result fly_catalog_mark_played(fly_app_t* app,
                                              const char* canonical_id_utf8,
                                              std::uint32_t canonical_id_utf8_length)
{
    if (app == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    const fly_result id_validation = validate_canonical_id(canonical_id_utf8,
                                                           canonical_id_utf8_length);
    if (id_validation != FLY_RESULT_OK)
    {
        return id_validation;
    }
    try
    {
        const std::string_view canonical_id(canonical_id_utf8, canonical_id_utf8_length);
        std::lock_guard<std::mutex> lock(app->state->mutex);
        auto next = std::make_shared<CatalogData>(*app->state->catalog);
        if (next->next_play_sequence == std::numeric_limits<std::uint64_t>::max())
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        UserRecord& user = upsert_user(*next, canonical_id);
        if (user.play_count == std::numeric_limits<std::uint32_t>::max())
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        ++user.play_count;
        user.last_played_sequence = ++next->next_play_sequence;
        if (!flynes::app::save_catalog(app->state->data_root, *next))
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        app->state->catalog = std::move(next);
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" fly_result fly_source_status_count(const fly_app_t* app, std::uint64_t* count_out)
{
    if (app == nullptr || count_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(app->state->mutex);
    *count_out = static_cast<std::uint64_t>(app->state->catalog->sources.size());
    return FLY_RESULT_OK;
}

extern "C" fly_result fly_source_status_get(const fly_app_t* app,
                                            std::uint64_t index,
                                            fly_source_status* status_out)
{
    if (app == nullptr || status_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (status_out->struct_size < FLY_SOURCE_STATUS_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (status_out->version != FLY_SOURCE_STATUS_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    try
    {
        std::lock_guard<std::mutex> lock(app->state->mutex);
        const CatalogData& catalog = *app->state->catalog;
        if (index >= catalog.sources.size())
        {
            return FLY_RESULT_OUT_OF_RANGE;
        }
        const SourceRecord& source = catalog.sources[static_cast<std::size_t>(index)];
        fly_source_status output = *status_out;
        std::copy(source.key.uuid.begin(), source.key.uuid.end(), output.source_uuid);
        output.source_scope = source.key.scope;
        output.last_completeness = source.last_completeness;
        output.freshness = source_freshness_summary(catalog, source.key);
        *status_out = output;
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" fly_result fly_source_remove(fly_app_t* app,
                                        const std::uint8_t source_uuid[16],
                                        std::uint32_t source_scope)
{
    if (app == nullptr || source_uuid == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (is_zero_uuid(source_uuid) || !is_valid_scope(source_scope))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (source_scope == FLY_SOURCE_SCOPE_BUILTIN)
    {
        return FLY_RESULT_FORBIDDEN;
    }
    SourceKey target{};
    std::copy(source_uuid, source_uuid + 16, target.uuid.begin());
    target.scope = source_scope;
    try
    {
        std::lock_guard<std::mutex> lock(app->state->mutex);
        const CatalogData& current = *app->state->catalog;
        bool found = false;
        for (const SourceRecord& existing : current.sources)
        {
            if (same_source(existing.key, target))
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            return FLY_RESULT_NOT_FOUND;
        }
        if (current.generation == std::numeric_limits<std::uint64_t>::max())
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        auto next = std::make_shared<CatalogData>();
        next->generation = current.generation + 1u;
        next->users = current.users;
        next->next_favorite_revision = current.next_favorite_revision;
        next->next_play_sequence = current.next_play_sequence;
        next->sources.reserve(current.sources.size());
        for (const SourceRecord& existing : current.sources)
        {
            if (!same_source(existing.key, target))
            {
                next->sources.push_back(existing);
            }
        }
        next->entries.reserve(current.entries.size());
        for (const CatalogEntryData& entry : current.entries)
        {
            if (!same_source(entry.source, target))
            {
                next->entries.push_back(entry);
            }
        }
        if (!flynes::app::save_catalog(app->state->data_root, *next))
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        app->state->catalog = std::move(next);
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" fly_result fly_settings_get(const fly_app_t* app, fly_settings_snapshot* snapshot_out)
{
    if (app == nullptr || snapshot_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (snapshot_out->struct_size < FLY_SETTINGS_SNAPSHOT_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (snapshot_out->version != FLY_SETTINGS_SNAPSHOT_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (!valid_output_buffer(snapshot_out->locale_tag_utf8, snapshot_out->locale_tag_capacity) ||
        !valid_output_buffer(snapshot_out->last_played_id_utf8,
                             snapshot_out->last_played_id_capacity))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        std::lock_guard<std::mutex> lock(app->state->mutex);
        const SettingsData& settings = app->state->settings;
        const std::uint32_t locale_required = required_string_size(settings.locale_tag);
        const std::uint32_t last_required = required_string_size(settings.last_played_id);
        if (snapshot_out->locale_tag_capacity < locale_required ||
            snapshot_out->last_played_id_capacity < last_required)
        {
            snapshot_out->locale_tag_required = locale_required;
            snapshot_out->last_played_id_required = last_required;
            return FLY_RESULT_BUFFER_TOO_SMALL;
        }
        fly_settings_snapshot output = *snapshot_out;
        output.aspect_mode = settings.aspect_mode;
        output.video_quality_preset = settings.video_quality_preset;
        output.custom_refresh_policy = settings.custom_refresh_policy;
        output.custom_temporal_mode = settings.custom_temporal_mode;
        output.custom_spatial_mode = settings.custom_spatial_mode;
        output.custom_post_effect = settings.custom_post_effect;
        output.adaptive_protection = settings.adaptive_protection;
        output.layout_preset = settings.layout_preset;
        output.direction_mode = settings.direction_mode;
        output.button_scale = settings.button_scale;
        output.vertical_offset = settings.vertical_offset;
        output.control_opacity = settings.control_opacity;
        output.joystick_scale = settings.joystick_scale;
        output.dead_zone = settings.dead_zone;
        output.haptic_level = settings.haptic_level;
        output.distinct_ab_haptics = settings.distinct_ab_haptics;
        output.audio_enabled = settings.audio_enabled;
        output.audio_focus_policy = settings.audio_focus_policy;
        output.autosave_enabled = settings.autosave_enabled;
        output.locale_tag_required = locale_required;
        output.last_played_id_required = last_required;
        copy_string(snapshot_out->locale_tag_utf8, settings.locale_tag);
        copy_string(snapshot_out->last_played_id_utf8, settings.last_played_id);
        *snapshot_out = output;
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" fly_result fly_settings_apply(fly_app_t* app, const fly_settings_snapshot* snapshot)
{
    if (app == nullptr || snapshot == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    const fly_result validation = validate_settings_snapshot(*snapshot);
    if (validation != FLY_RESULT_OK)
    {
        return validation;
    }
    try
    {
        SettingsData next = settings_from_snapshot(*snapshot);
        std::lock_guard<std::mutex> lock(app->state->mutex);
        if (!flynes::app::save_settings(app->state->data_root, next))
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        app->state->settings = std::move(next);
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" fly_result fly_control_layout_get(const fly_app_t* app,
                                             char* utf8_out,
                                             std::uint32_t capacity,
                                             std::uint32_t* required_out)
{
    if (app == nullptr || required_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (!valid_output_buffer(utf8_out, capacity))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        std::lock_guard<std::mutex> lock(app->state->mutex);
        const std::uint32_t required = required_string_size(app->state->control_layout_utf8);
        *required_out = required;
        if (capacity < required)
        {
            return FLY_RESULT_BUFFER_TOO_SMALL;
        }
        if (utf8_out != nullptr)
        {
            copy_string(utf8_out, app->state->control_layout_utf8);
        }
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}

extern "C" fly_result fly_control_layout_apply(fly_app_t* app,
                                               const char* utf8,
                                               std::uint32_t utf8_length)
{
    if (app == nullptr || utf8 == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        const std::string input(utf8, utf8_length);
        std::string normalized;
        if (!is_valid_utf8(utf8, utf8_length, 65536u))
        {
            normalized = flynes::product::ControlLayoutV2::recommended().encode();
        }
        else
        {
            normalized = flynes::product::ControlLayoutV2::decode_or_recommended(input).encode();
        }
        std::lock_guard<std::mutex> lock(app->state->mutex);
        if (!flynes::app::save_control_layout(app->state->data_root, normalized))
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        app->state->control_layout_utf8 = std::move(normalized);
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&) { return FLY_RESULT_OUT_OF_MEMORY; }
    catch (...) { return FLY_RESULT_INTERNAL_ERROR; }
}
