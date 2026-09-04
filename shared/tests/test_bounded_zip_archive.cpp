#include "zip_open_fixture.hpp"

#include "catalog/bounded_zip_archive.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>
#include <unordered_set>
#include <vector>

#ifndef FLYNES_ZIP_OPEN_FIXTURE_DIR
#error "FLYNES_ZIP_OPEN_FIXTURE_DIR must identify the shared ZIP-open fixture directory"
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

void check_entry(const flynes::test::ZipFixtureEntry& expected,
                 const flynes::catalog::BoundedZipEntry& actual,
                 const std::string& case_id)
{
    check(actual.raw_name == expected.raw_name, case_id, "raw_name");
    check(actual.central_extra == expected.central_extra, case_id, "central_extra");
    check(actual.local_header_offset == expected.local_header_offset,
          case_id,
          "local_header_offset/identity");
    check(actual.flags == expected.flags, case_id, "flags");
    check(actual.method == expected.method, case_id, "method");
    check(actual.crc32 == expected.crc32, case_id, "crc32");
    check(actual.compressed_size == expected.compressed_size, case_id, "compressed_size");
    check(actual.uncompressed_size == expected.uncompressed_size,
          case_id,
          "uncompressed_size");
    check(actual.is_directory == expected.directory, case_id, "directory");
}

} // namespace

using flynes::catalog::ZipOpenCode;

static_assert(static_cast<std::uint8_t>(ZipOpenCode::INVALID_ZIP) == 0u,
              "INVALID_ZIP value changed");
static_assert(static_cast<std::uint8_t>(ZipOpenCode::PACKAGE_LIMIT_EXCEEDED) == 1u,
              "PACKAGE_LIMIT_EXCEEDED value changed");
static_assert(static_cast<std::uint8_t>(ZipOpenCode::ENTRY_LIMIT_EXCEEDED) == 2u,
              "ENTRY_LIMIT_EXCEEDED value changed");
static_assert(static_cast<std::uint8_t>(ZipOpenCode::INFLATED_LIMIT_EXCEEDED) == 3u,
              "INFLATED_LIMIT_EXCEEDED value changed");
static_assert(static_cast<std::uint8_t>(ZipOpenCode::PAYLOAD_LIMIT_EXCEEDED) == 4u,
              "PAYLOAD_LIMIT_EXCEEDED value changed");
static_assert(static_cast<std::uint8_t>(ZipOpenCode::NAME_LIMIT_EXCEEDED) == 5u,
              "NAME_LIMIT_EXCEEDED value changed");
static_assert(static_cast<std::uint8_t>(ZipOpenCode::RATIO_LIMIT_EXCEEDED) == 6u,
              "RATIO_LIMIT_EXCEEDED value changed");
static_assert(static_cast<std::uint8_t>(ZipOpenCode::ENCRYPTED) == 7u,
              "ENCRYPTED value changed");
static_assert(static_cast<std::uint8_t>(ZipOpenCode::UNSUPPORTED_COMPRESSION) == 8u,
              "UNSUPPORTED_COMPRESSION value changed");
static_assert(static_cast<std::uint8_t>(ZipOpenCode::ENTRY_MISSING) == 9u,
              "ENTRY_MISSING value changed");

int main()
{
    try
    {
        const std::vector<flynes::test::ZipOpenFixture> fixtures =
            flynes::test::load_zip_open_fixtures(FLYNES_ZIP_OPEN_FIXTURE_DIR);
        check(fixtures.size() == 60u, "fixture_corpus", "exactly 60 cases");
        std::unordered_set<std::string> case_ids;
        for (const flynes::test::ZipOpenFixture& fixture : fixtures)
        {
            check(case_ids.insert(fixture.case_id).second, fixture.case_id, "unique case ID");
            const auto limits = flynes::catalog::BoundedZipLimits::create(
                fixture.limits.max_package_bytes,
                fixture.limits.max_payload_bytes,
                fixture.limits.max_zip_entries,
                fixture.limits.max_cumulative_inflated_bytes,
                fixture.limits.max_name_bytes,
                fixture.limits.max_compression_ratio,
                fixture.limits.ratio_guard_threshold_bytes);
            check(limits.has_value(), fixture.case_id, "fixture limits are valid");
            if (!limits.has_value())
            {
                continue;
            }
            const flynes::catalog::BoundedZipOpenResult actual =
                flynes::catalog::open_bounded_zip(
                    fixture.archive.data(), fixture.archive.size(), *limits);
            check(actual.succeeded() == fixture.succeeds, fixture.case_id, "outcome");
            if (fixture.succeeds)
            {
                if (!actual.succeeded())
                {
                    if (actual.error() != nullptr)
                    {
                        std::fprintf(stderr,
                                     "  got %s: %s\n",
                                     flynes::catalog::zip_open_code_name(actual.error()->code),
                                     actual.error()->message.c_str());
                    }
                    continue;
                }
                const flynes::catalog::BoundedZipArchive* const archive = actual.archive();
                check(archive != nullptr, fixture.case_id, "archive result");
                if (archive == nullptr)
                {
                    continue;
                }
                check(archive->physical_size() == fixture.archive.size(),
                      fixture.case_id,
                      "one retained physical archive size");
                check(archive->limits().max_payload_bytes() ==
                          fixture.limits.max_payload_bytes,
                      fixture.case_id,
                      "payload limit retained for later payload work");
                check(archive->entries().size() == fixture.entries.size(),
                      fixture.case_id,
                      "entry count");
                if (archive->entries().size() == fixture.entries.size())
                {
                    for (std::size_t index = 0; index < fixture.entries.size(); ++index)
                    {
                        check_entry(fixture.entries[index],
                                    archive->entries()[index],
                                    fixture.case_id + " entry " + std::to_string(index));
                    }
                }
            }
            else
            {
                const flynes::catalog::BoundedZipOpenError* const error = actual.error();
                check(error != nullptr, fixture.case_id, "error result");
                if (error != nullptr)
                {
                    check(flynes::catalog::zip_open_code_name(error->code) == fixture.error_code,
                          fixture.case_id,
                          "error code");
                    check(error->message == fixture.error_message,
                          fixture.case_id,
                          "error message");
                }
            }
        }
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "FAIL: fixture harness: %s\n", failure.what());
        ++failures;
    }

    if (failures == 0)
    {
        std::puts("flynes_bounded_zip_archive_test: PASS (60 fixtures)");
    }
    return failures == 0 ? 0 : 1;
}
