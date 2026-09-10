#include "rom_fixture.hpp"

#include "catalog/rom_payload_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <unordered_set>
#include <vector>

#ifndef FLYNES_ROM_FIXTURE_DIR
#error "FLYNES_ROM_FIXTURE_DIR must identify the shared ROM fixture directory"
#endif

namespace {

int failures = 0;

void check(bool condition, const std::string& case_id, const char* field)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s: %s\n", case_id.c_str(), field);
        ++failures;
    }
}

void check_analysis(const flynes::catalog::RomAnalysis& expected,
                    const flynes::catalog::RomAnalysis& actual,
                    const std::string& case_id)
{
    check(actual.expected_bytes == expected.expected_bytes, case_id, "expected_bytes");
    check(actual.actual_bytes == expected.actual_bytes, case_id, "actual_bytes");
    check(actual.prg_bytes == expected.prg_bytes, case_id, "prg_bytes");
    check(actual.chr_bytes == expected.chr_bytes, case_id, "chr_bytes");
    check(actual.mapper == expected.mapper, case_id, "mapper");
    check(actual.submapper == expected.submapper, case_id, "submapper");
    check(actual.trainer == expected.trainer, case_id, "trainer");
    check(actual.battery == expected.battery, case_id, "battery");
    check(actual.disk_sides == expected.disk_sides, case_id, "disk_sides");
    check(actual.warning_count == expected.warning_count, case_id, "warning_count");
    if (actual.warning_count == expected.warning_count &&
        actual.warning_count <= actual.warnings.size())
    {
        for (std::size_t index = 0; index < actual.warning_count; ++index)
        {
            check(actual.warnings[index] == expected.warnings[index],
                  case_id,
                  "warning order/value");
        }
    }
}

void check_required_cases(const std::unordered_set<std::string>& case_ids)
{
    const std::vector<std::string> required = {
        "playable_ines_metadata",
        "dirty_header_ines",
        "nes_header_too_short",
        "zero_prg_ines",
        "truncated_ines",
        "nes2_extended_mapper_metadata",
        "nes2_exponential_64_96",
        "nes2_exponential_multiplier_overflow",
        "valid_headered_fds",
        "valid_headerless_fds",
        "zero_side_fds",
        "truncated_headered_fds",
        "invalid_headered_fds_signature",
        "valid_unif_positive_prg",
        "unif_missing_prg",
        "unif_truncated_chunk_data",
        "unknown_bytes",
    };
    for (const std::string& case_id : required)
    {
        check(case_ids.count(case_id) == 1u, case_id, "required fixture is present");
    }
}

} // namespace

using flynes::catalog::CompatibilityReason;
using flynes::catalog::CompatibilityState;
using flynes::catalog::RomFormat;
using flynes::catalog::RomWarning;

static_assert(static_cast<std::uint8_t>(RomFormat::INES) == 0u, "INES value changed");
static_assert(static_cast<std::uint8_t>(RomFormat::NES2) == 1u, "NES2 value changed");
static_assert(static_cast<std::uint8_t>(RomFormat::FDS) == 2u, "FDS value changed");
static_assert(static_cast<std::uint8_t>(RomFormat::UNIF) == 3u, "UNIF value changed");
static_assert(static_cast<std::uint8_t>(RomFormat::UNKNOWN) == 4u, "UNKNOWN value changed");

static_assert(static_cast<std::uint8_t>(CompatibilityState::PLAYABLE) == 0u,
              "PLAYABLE value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityState::UNSUPPORTED) == 1u,
              "UNSUPPORTED value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityState::INVALID) == 2u,
              "INVALID value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityState::UNKNOWN) == 3u,
              "UNKNOWN state value changed");

static_assert(static_cast<std::uint8_t>(CompatibilityReason::PLAYABLE_NES) == 0u,
              "PLAYABLE_NES value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityReason::FDS_BIOS_API_NOT_IMPLEMENTED) == 1u,
              "FDS reason value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityReason::UNIF_PRODUCT_DISABLED) == 2u,
              "UNIF reason value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityReason::NES_HEADER_INVALID) == 3u,
              "NES_HEADER_INVALID value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityReason::NES_ZERO_PRG) == 4u,
              "NES_ZERO_PRG value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityReason::NES_TRUNCATED) == 5u,
              "NES_TRUNCATED value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityReason::NES_SIZE_OVERFLOW) == 6u,
              "NES_SIZE_OVERFLOW value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityReason::FDS_INVALID_HEADER) == 7u,
              "FDS_INVALID_HEADER value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityReason::FDS_INVALID_SIDE_COUNT) == 8u,
              "FDS_INVALID_SIDE_COUNT value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityReason::FDS_TRUNCATED) == 9u,
              "FDS_TRUNCATED value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityReason::UNIF_INVALID_CHUNK) == 10u,
              "UNIF_INVALID_CHUNK value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityReason::UNIF_MISSING_PRG) == 11u,
              "UNIF_MISSING_PRG value changed");
static_assert(static_cast<std::uint8_t>(CompatibilityReason::UNKNOWN_FORMAT) == 12u,
              "UNKNOWN_FORMAT value changed");

static_assert(static_cast<std::uint8_t>(RomWarning::TRAILING_DATA) == 0u,
              "TRAILING_DATA value changed");
static_assert(static_cast<std::uint8_t>(RomWarning::DIRTY_HEADER) == 1u,
              "DIRTY_HEADER value changed");
static_assert(static_cast<std::uint8_t>(RomWarning::UNICODE_PATH_REJECTED) == 2u,
              "UNICODE_PATH_REJECTED value changed");

int main()
{
    try
    {
        const std::vector<flynes::test::RomFixture> fixtures =
            flynes::test::load_rom_fixtures(FLYNES_ROM_FIXTURE_DIR);
        std::unordered_set<std::string> case_ids;
        for (const flynes::test::RomFixture& fixture : fixtures)
        {
            case_ids.insert(fixture.case_id);
            const flynes::catalog::RomParseResult actual =
                flynes::catalog::parse_rom_payload(
                    {fixture.payload.data(), fixture.payload.size()});
            check(actual.recognized == fixture.expected.recognized,
                  fixture.case_id,
                  "recognized");
            check(actual.format == fixture.expected.format, fixture.case_id, "format");
            check(actual.state == fixture.expected.state, fixture.case_id, "state");
            check(actual.reason == fixture.expected.reason, fixture.case_id, "reason");
            check_analysis(fixture.expected.analysis, actual.analysis, fixture.case_id);
        }
        check_required_cases(case_ids);

        const flynes::catalog::RomParseResult empty =
            flynes::catalog::parse_rom_payload({nullptr, 0u});
        check(!empty.recognized, "empty_byte_view", "recognized");
        check(empty.analysis.actual_bytes == 0u, "empty_byte_view", "actual_bytes");
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "FAIL: fixture harness: %s\n", failure.what());
        ++failures;
    }

    if (failures == 0)
    {
        std::puts("flynes_rom_payload_parser_test: PASS (25 fixtures)");
    }
    return failures == 0 ? 0 : 1;
}
