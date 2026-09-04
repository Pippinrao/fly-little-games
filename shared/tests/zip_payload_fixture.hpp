#ifndef FLYNES_TESTS_ZIP_PAYLOAD_FIXTURE_HPP
#define FLYNES_TESTS_ZIP_PAYLOAD_FIXTURE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace flynes::test {

struct ZipPayloadFixtureLimits final
{
    std::uint64_t max_package_bytes = 0;
    std::uint64_t max_payload_bytes = 0;
    std::uint32_t max_zip_entries = 0;
    std::uint64_t max_cumulative_inflated_bytes = 0;
    std::uint32_t max_name_bytes = 0;
    std::uint32_t max_compression_ratio = 0;
    std::uint64_t ratio_guard_threshold_bytes = 0;
};

struct ZipPayloadFixture final
{
    std::string case_id;
    std::vector<std::uint8_t> archive;
    ZipPayloadFixtureLimits limits;
    std::vector<std::uint8_t> selector_raw_name;
    std::int32_t selector_local_header_offset = 0;
    bool succeeds = false;
    std::string error_code;
    std::string error_message;
    std::uint32_t payload_length = 0;
    std::string payload_sha256;
};

std::vector<ZipPayloadFixture> load_zip_payload_fixtures(
    const std::string& fixture_root);

std::string fixture_sha256(const std::vector<std::uint8_t>& bytes);

} // namespace flynes::test

#endif
