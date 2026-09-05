#ifndef FLYNES_APP_CATALOG_STATE_HPP
#define FLYNES_APP_CATALOG_STATE_HPP

#include <flynes/flynes_app.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace flynes::app {

struct SourceKey final
{
    std::array<std::uint8_t, 16> uuid{};
    std::uint32_t scope = 0u;
};

inline bool same_source(const SourceKey& left, const SourceKey& right) noexcept
{
    return left.scope == right.scope && left.uuid == right.uuid;
}

struct SourceRecord final
{
    SourceKey key;
    std::uint32_t last_completeness = FLY_SCAN_COMPLETENESS_FULL;
};

struct UserRecord final
{
    std::string canonical_id;
    bool favorite = false;
    std::uint64_t favorite_revision = 0u;
    std::uint64_t last_played_sequence = 0u;
    std::uint32_t play_count = 0u;
};

struct CatalogEntryData final
{
    SourceKey source;
    std::uint64_t payload_size = 0u;
    std::uint64_t physical_size = 0u;
    std::uint64_t expected_bytes = 0u;
    std::uint64_t prg_bytes = 0u;
    std::uint64_t chr_bytes = 0u;
    std::int32_t mapper = -1;
    std::int32_t submapper = -1;
    std::uint32_t disk_sides = 0u;
    std::array<std::uint8_t, 20> payload_sha1{};
    std::array<std::uint8_t, 32> payload_sha256{};
    std::array<std::uint8_t, 32> physical_sha256{};
    std::array<std::uint8_t, 4> payload_crc32{};
    std::uint32_t package_format = 0u;
    std::uint32_t rom_format = 0u;
    std::uint32_t compatibility_state = 0u;
    std::uint32_t compatibility_reason = 0u;
    std::uint32_t freshness = FLY_CATALOG_FRESHNESS_FRESH;
    std::uint32_t flags = 0u;
    std::string canonical_id;
    std::string variant_id;
    std::string display_name;
    std::string source_relative_path;
    std::string package_id;
    std::vector<std::uint8_t> zip_raw_name;
    std::int32_t zip_local_header_offset = -1;
};

struct CatalogData final
{
    std::uint64_t generation = 0u;
    std::vector<CatalogEntryData> entries;
    std::vector<SourceRecord> sources;
    std::vector<UserRecord> users;
    std::uint64_t next_favorite_revision = 0u;
    std::uint64_t next_play_sequence = 0u;
};

} // namespace flynes::app

#endif
