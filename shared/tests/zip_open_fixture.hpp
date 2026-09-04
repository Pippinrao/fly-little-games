#ifndef FLYNES_TESTS_ZIP_OPEN_FIXTURE_HPP
#define FLYNES_TESTS_ZIP_OPEN_FIXTURE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace flynes::test {

struct ZipFixtureLimits final
{
    std::uint64_t max_package_bytes = 0;
    std::uint64_t max_payload_bytes = 0;
    std::uint32_t max_zip_entries = 0;
    std::uint64_t max_cumulative_inflated_bytes = 0;
    std::uint32_t max_name_bytes = 0;
    std::uint32_t max_compression_ratio = 0;
    std::uint64_t ratio_guard_threshold_bytes = 0;
};

struct ZipFixtureEntry final
{
    std::vector<std::uint8_t> raw_name;
    std::vector<std::uint8_t> central_extra;
    std::int32_t local_header_offset = 0;
    std::uint16_t flags = 0;
    std::uint16_t method = 0;
    std::uint32_t crc32 = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
    bool directory = false;
};

struct ZipOpenFixture final
{
    std::string case_id;
    std::vector<std::uint8_t> archive;
    ZipFixtureLimits limits;
    bool succeeds = false;
    std::string error_code;
    std::string error_message;
    std::vector<ZipFixtureEntry> entries;
};

std::vector<ZipOpenFixture> load_zip_open_fixtures(const std::string& fixture_root);

} // namespace flynes::test

#endif
