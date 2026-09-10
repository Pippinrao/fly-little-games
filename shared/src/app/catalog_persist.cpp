#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "app/catalog_persist.hpp"

#include "catalog/content_identity.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <new>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace flynes::app {
namespace {

constexpr char kMagic[] = "FLYCAT01";
constexpr std::uint32_t kVersion = 1u;
constexpr std::uint32_t kMaxStringBytes = 65536u;
constexpr std::uint32_t kMaxCount = 100000u;

std::filesystem::path root_path(const std::string& data_root_utf8)
{
    return std::filesystem::u8path(data_root_utf8);
}

std::filesystem::path live_path(const std::filesystem::path& root)
{
    return root / "catalog.flycat01";
}

std::filesystem::path tmp_path(const std::filesystem::path& root)
{
    return root / "catalog.flycat01.tmp";
}

std::filesystem::path bak_path(const std::filesystem::path& root)
{
    return root / "catalog.flycat01.bak";
}

FILE* open_write(const std::filesystem::path& path)
{
#if defined(_WIN32)
    return _wfopen(path.wstring().c_str(), L"wb");
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

FILE* open_read(const std::filesystem::path& path)
{
#if defined(_WIN32)
    return _wfopen(path.wstring().c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

bool flush_sync_close(FILE* file)
{
    if (file == nullptr)
    {
        return false;
    }
    if (std::fflush(file) != 0)
    {
        std::fclose(file);
        return false;
    }
#if defined(_WIN32)
    if (_commit(_fileno(file)) != 0)
    {
        std::fclose(file);
        return false;
    }
#else
    if (fsync(fileno(file)) != 0)
    {
        std::fclose(file);
        return false;
    }
#endif
    return std::fclose(file) == 0;
}

bool read_file_bounded(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes)
{
    bytes.clear();
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
    {
        return false;
    }
    FILE* file = open_read(path);
    if (file == nullptr)
    {
        return false;
    }
    std::array<std::uint8_t, 8192> buffer{};
    std::uint64_t total = 0u;
    for (;;)
    {
        const std::size_t amount = std::fread(buffer.data(), 1u, buffer.size(), file);
        if (amount == 0u)
        {
            const bool ok = std::feof(file) != 0 && std::ferror(file) == 0;
            std::fclose(file);
            return ok;
        }
        if (total > FLYCAT01_MAX_BYTES || amount > FLYCAT01_MAX_BYTES - total)
        {
            std::fclose(file);
            bytes.clear();
            return false;
        }
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(amount));
        total += static_cast<std::uint64_t>(amount);
    }
}

bool write_file_sync(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    FILE* file = open_write(path);
    if (file == nullptr)
    {
        return false;
    }
    if (!bytes.empty())
    {
        if (std::fwrite(bytes.data(), 1u, bytes.size(), file) != bytes.size())
        {
            std::fclose(file);
            return false;
        }
    }
    return flush_sync_close(file);
}

bool replace_live(const std::filesystem::path& live,
                  const std::filesystem::path& tmp,
                  const std::filesystem::path& bak)
{
    std::error_code error;
    if (std::filesystem::exists(live, error))
    {
        std::filesystem::remove(bak, error);
        std::filesystem::rename(live, bak, error);
        if (error)
        {
            std::filesystem::remove(tmp, error);
            return false;
        }
    }
    std::filesystem::rename(tmp, live, error);
    if (error)
    {
        std::error_code restore;
        if (std::filesystem::exists(bak, restore))
        {
            std::filesystem::rename(bak, live, restore);
        }
        std::filesystem::remove(tmp, error);
        return false;
    }
    return true;
}

bool sha256_bytes(const std::uint8_t* data, std::size_t size, std::array<std::uint8_t, 32>& out)
{
    const auto hex = flynes::catalog::sha256_hex(flynes::catalog::ContentBytes{data, size});
    if (!hex.ok() || hex.value.size() != 64u)
    {
        return false;
    }
    const auto nibble = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        return -1;
    };
    for (std::size_t index = 0; index < 32u; ++index)
    {
        const int high = nibble(hex.value[index * 2u]);
        const int low = nibble(hex.value[index * 2u + 1u]);
        if (high < 0 || low < 0)
        {
            return false;
        }
        out[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

class Writer final
{
public:
    bool put_u8(std::uint8_t value)
    {
        if (bytes_.size() >= FLYCAT01_MAX_BYTES)
        {
            return false;
        }
        bytes_.push_back(value);
        return true;
    }

    bool put_u32(std::uint32_t value)
    {
        return put_u8(static_cast<std::uint8_t>(value)) &&
               put_u8(static_cast<std::uint8_t>(value >> 8u)) &&
               put_u8(static_cast<std::uint8_t>(value >> 16u)) &&
               put_u8(static_cast<std::uint8_t>(value >> 24u));
    }

    bool put_i32(std::int32_t value)
    {
        return put_u32(static_cast<std::uint32_t>(value));
    }

    bool put_u64(std::uint64_t value)
    {
        return put_u32(static_cast<std::uint32_t>(value)) &&
               put_u32(static_cast<std::uint32_t>(value >> 32u));
    }

    bool put_bytes(const std::uint8_t* data, std::size_t size)
    {
        if (size > FLYCAT01_MAX_BYTES - bytes_.size())
        {
            return false;
        }
        bytes_.insert(bytes_.end(), data, data + size);
        return true;
    }

    bool put_blob(const std::uint8_t* data, std::size_t size)
    {
        if (size > kMaxStringBytes)
        {
            return false;
        }
        return put_u32(static_cast<std::uint32_t>(size)) && put_bytes(data, size);
    }

    bool put_string(const std::string& value)
    {
        return put_blob(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
    }

    std::vector<std::uint8_t>& bytes() { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

class Reader final
{
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    bool remaining(std::size_t size) const noexcept
    {
        return offset_ <= bytes_.size() && bytes_.size() - offset_ >= size;
    }

    bool take_u8(std::uint8_t& value)
    {
        if (!remaining(1u))
        {
            return false;
        }
        value = bytes_[offset_++];
        return true;
    }

    bool take_u32(std::uint32_t& value)
    {
        std::uint8_t b0 = 0;
        std::uint8_t b1 = 0;
        std::uint8_t b2 = 0;
        std::uint8_t b3 = 0;
        if (!take_u8(b0) || !take_u8(b1) || !take_u8(b2) || !take_u8(b3))
        {
            return false;
        }
        value = static_cast<std::uint32_t>(b0) |
                (static_cast<std::uint32_t>(b1) << 8u) |
                (static_cast<std::uint32_t>(b2) << 16u) |
                (static_cast<std::uint32_t>(b3) << 24u);
        return true;
    }

    bool take_i32(std::int32_t& value)
    {
        std::uint32_t raw = 0u;
        if (!take_u32(raw))
        {
            return false;
        }
        value = static_cast<std::int32_t>(raw);
        return true;
    }

    bool take_u64(std::uint64_t& value)
    {
        std::uint32_t low = 0u;
        std::uint32_t high = 0u;
        if (!take_u32(low) || !take_u32(high))
        {
            return false;
        }
        value = static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(high) << 32u);
        return true;
    }

    bool take_bytes(std::uint8_t* destination, std::size_t size)
    {
        if (!remaining(size))
        {
            return false;
        }
        std::memcpy(destination, bytes_.data() + offset_, size);
        offset_ += size;
        return true;
    }

    bool take_blob(std::vector<std::uint8_t>& value)
    {
        std::uint32_t size = 0u;
        if (!take_u32(size) || size > kMaxStringBytes || !remaining(size))
        {
            return false;
        }
        value.assign(bytes_.data() + offset_, bytes_.data() + offset_ + size);
        offset_ += size;
        return true;
    }

    bool take_string(std::string& value)
    {
        std::vector<std::uint8_t> blob;
        if (!take_blob(blob))
        {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(blob.data()), blob.size());
        return true;
    }

    bool at_end() const noexcept { return offset_ == bytes_.size(); }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0u;
};

bool encode_payload(const CatalogData& catalog, Writer& writer)
{
    if (catalog.sources.size() > kMaxCount || catalog.entries.size() > kMaxCount ||
        catalog.users.size() > kMaxCount)
    {
        return false;
    }
    if (!writer.put_u64(catalog.generation) ||
        !writer.put_u32(static_cast<std::uint32_t>(catalog.sources.size())))
    {
        return false;
    }
    for (const SourceRecord& source : catalog.sources)
    {
        if (!writer.put_bytes(source.key.uuid.data(), source.key.uuid.size()) ||
            !writer.put_u32(source.key.scope) ||
            !writer.put_u32(source.last_completeness))
        {
            return false;
        }
    }
    if (!writer.put_u32(static_cast<std::uint32_t>(catalog.entries.size())))
    {
        return false;
    }
    for (const CatalogEntryData& entry : catalog.entries)
    {
        if (!writer.put_bytes(entry.source.uuid.data(), entry.source.uuid.size()) ||
            !writer.put_u32(entry.source.scope) ||
            !writer.put_u64(entry.payload_size) ||
            !writer.put_u64(entry.physical_size) ||
            !writer.put_u64(entry.expected_bytes) ||
            !writer.put_u64(entry.prg_bytes) ||
            !writer.put_u64(entry.chr_bytes) ||
            !writer.put_i32(entry.mapper) ||
            !writer.put_i32(entry.submapper) ||
            !writer.put_u32(entry.disk_sides) ||
            !writer.put_bytes(entry.payload_sha1.data(), entry.payload_sha1.size()) ||
            !writer.put_bytes(entry.payload_sha256.data(), entry.payload_sha256.size()) ||
            !writer.put_bytes(entry.physical_sha256.data(), entry.physical_sha256.size()) ||
            !writer.put_bytes(entry.payload_crc32.data(), entry.payload_crc32.size()) ||
            !writer.put_u32(entry.package_format) ||
            !writer.put_u32(entry.rom_format) ||
            !writer.put_u32(entry.compatibility_state) ||
            !writer.put_u32(entry.compatibility_reason) ||
            !writer.put_u32(entry.freshness) ||
            !writer.put_u32(entry.flags) ||
            !writer.put_string(entry.canonical_id) ||
            !writer.put_string(entry.variant_id) ||
            !writer.put_string(entry.display_name) ||
            !writer.put_string(entry.source_relative_path) ||
            !writer.put_string(entry.package_id) ||
            !writer.put_blob(entry.zip_raw_name.data(), entry.zip_raw_name.size()) ||
            !writer.put_i32(entry.zip_local_header_offset))
        {
            return false;
        }
    }
    if (!writer.put_u32(static_cast<std::uint32_t>(catalog.users.size())))
    {
        return false;
    }
    for (const UserRecord& user : catalog.users)
    {
        if (!writer.put_string(user.canonical_id) ||
            !writer.put_u8(user.favorite ? 1u : 0u) ||
            !writer.put_u64(user.favorite_revision) ||
            !writer.put_u64(user.last_played_sequence) ||
            !writer.put_u32(user.play_count))
        {
            return false;
        }
    }
    return writer.put_u64(catalog.next_favorite_revision) &&
           writer.put_u64(catalog.next_play_sequence);
}

bool decode_payload(Reader& reader, CatalogData& catalog)
{
    std::uint32_t source_count = 0u;
    if (!reader.take_u64(catalog.generation) || !reader.take_u32(source_count) ||
        source_count > kMaxCount)
    {
        return false;
    }
    catalog.sources.reserve(source_count);
    for (std::uint32_t index = 0u; index < source_count; ++index)
    {
        SourceRecord source;
        if (!reader.take_bytes(source.key.uuid.data(), source.key.uuid.size()) ||
            !reader.take_u32(source.key.scope) ||
            !reader.take_u32(source.last_completeness))
        {
            return false;
        }
        catalog.sources.push_back(std::move(source));
    }
    std::uint32_t entry_count = 0u;
    if (!reader.take_u32(entry_count) || entry_count > kMaxCount)
    {
        return false;
    }
    catalog.entries.reserve(entry_count);
    for (std::uint32_t index = 0u; index < entry_count; ++index)
    {
        CatalogEntryData entry;
        if (!reader.take_bytes(entry.source.uuid.data(), entry.source.uuid.size()) ||
            !reader.take_u32(entry.source.scope) ||
            !reader.take_u64(entry.payload_size) ||
            !reader.take_u64(entry.physical_size) ||
            !reader.take_u64(entry.expected_bytes) ||
            !reader.take_u64(entry.prg_bytes) ||
            !reader.take_u64(entry.chr_bytes) ||
            !reader.take_i32(entry.mapper) ||
            !reader.take_i32(entry.submapper) ||
            !reader.take_u32(entry.disk_sides) ||
            !reader.take_bytes(entry.payload_sha1.data(), entry.payload_sha1.size()) ||
            !reader.take_bytes(entry.payload_sha256.data(), entry.payload_sha256.size()) ||
            !reader.take_bytes(entry.physical_sha256.data(), entry.physical_sha256.size()) ||
            !reader.take_bytes(entry.payload_crc32.data(), entry.payload_crc32.size()) ||
            !reader.take_u32(entry.package_format) ||
            !reader.take_u32(entry.rom_format) ||
            !reader.take_u32(entry.compatibility_state) ||
            !reader.take_u32(entry.compatibility_reason) ||
            !reader.take_u32(entry.freshness) ||
            !reader.take_u32(entry.flags) ||
            !reader.take_string(entry.canonical_id) ||
            !reader.take_string(entry.variant_id) ||
            !reader.take_string(entry.display_name) ||
            !reader.take_string(entry.source_relative_path) ||
            !reader.take_string(entry.package_id) ||
            !reader.take_blob(entry.zip_raw_name) ||
            !reader.take_i32(entry.zip_local_header_offset))
        {
            return false;
        }
        catalog.entries.push_back(std::move(entry));
    }
    std::uint32_t user_count = 0u;
    if (!reader.take_u32(user_count) || user_count > kMaxCount)
    {
        return false;
    }
    catalog.users.reserve(user_count);
    for (std::uint32_t index = 0u; index < user_count; ++index)
    {
        UserRecord user;
        std::uint8_t favorite = 0u;
        if (!reader.take_string(user.canonical_id) ||
            !reader.take_u8(favorite) ||
            favorite > 1u ||
            !reader.take_u64(user.favorite_revision) ||
            !reader.take_u64(user.last_played_sequence) ||
            !reader.take_u32(user.play_count))
        {
            return false;
        }
        user.favorite = favorite != 0u;
        catalog.users.push_back(std::move(user));
    }
    return reader.take_u64(catalog.next_favorite_revision) &&
           reader.take_u64(catalog.next_play_sequence) &&
           reader.at_end();
}

bool decode_file(const std::vector<std::uint8_t>& encoded, CatalogData& catalog)
{
    if (encoded.size() < 8u + 4u + 32u || encoded.size() > FLYCAT01_MAX_BYTES)
    {
        return false;
    }
    if (std::memcmp(encoded.data(), kMagic, 8) != 0)
    {
        return false;
    }
    const std::uint32_t version = static_cast<std::uint32_t>(encoded[8]) |
                                  (static_cast<std::uint32_t>(encoded[9]) << 8u) |
                                  (static_cast<std::uint32_t>(encoded[10]) << 16u) |
                                  (static_cast<std::uint32_t>(encoded[11]) << 24u);
    if (version != kVersion)
    {
        return false;
    }
    std::array<std::uint8_t, 32> digest{};
    if (!sha256_bytes(encoded.data(), encoded.size() - 32u, digest) ||
        std::memcmp(digest.data(), encoded.data() + encoded.size() - 32u, 32) != 0)
    {
        return false;
    }
    std::vector<std::uint8_t> payload(encoded.begin() + 12, encoded.end() - 32);
    Reader reader(payload);
    return decode_payload(reader, catalog);
}

} // namespace

std::shared_ptr<CatalogData> load_catalog(const std::string& data_root_utf8)
{
    auto empty = std::make_shared<CatalogData>();
    const std::filesystem::path root = root_path(data_root_utf8);
    const std::filesystem::path live = live_path(root);
    const std::filesystem::path bak = bak_path(root);
    std::error_code error;
    std::filesystem::path chosen;
    if (std::filesystem::is_regular_file(live, error))
    {
        chosen = live;
    }
    else if (std::filesystem::is_regular_file(bak, error))
    {
        chosen = bak;
    }
    else
    {
        return empty;
    }
    std::vector<std::uint8_t> encoded;
    if (!read_file_bounded(chosen, encoded))
    {
        return empty;
    }
    auto loaded = std::make_shared<CatalogData>();
    if (!decode_file(encoded, *loaded))
    {
        return empty;
    }
    return loaded;
}

bool save_catalog(const std::string& data_root_utf8, const CatalogData& catalog)
{
    Writer payload;
    if (!encode_payload(catalog, payload))
    {
        return false;
    }
    Writer file;
    if (!file.put_bytes(reinterpret_cast<const std::uint8_t*>(kMagic), 8u) ||
        !file.put_u32(kVersion) ||
        !file.put_bytes(payload.bytes().data(), payload.bytes().size()))
    {
        return false;
    }
    std::array<std::uint8_t, 32> digest{};
    if (!sha256_bytes(file.bytes().data(), file.bytes().size(), digest) ||
        !file.put_bytes(digest.data(), digest.size()) ||
        file.bytes().size() > FLYCAT01_MAX_BYTES)
    {
        return false;
    }

    const std::filesystem::path root = root_path(data_root_utf8);
    std::error_code error;
    std::filesystem::create_directories(root, error);
    if (error)
    {
        return false;
    }
    const std::filesystem::path live = live_path(root);
    const std::filesystem::path tmp = tmp_path(root);
    const std::filesystem::path bak = bak_path(root);
    if (!write_file_sync(tmp, file.bytes()))
    {
        std::filesystem::remove(tmp, error);
        return false;
    }
    return replace_live(live, tmp, bak);
}

} // namespace flynes::app
