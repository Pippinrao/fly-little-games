#include "catalog/bounded_zip_archive.hpp"
#include "zip_payload_fixture.hpp"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#ifndef FLYNES_ZIP_PAYLOAD_FIXTURE_DIR
#error "FLYNES_ZIP_PAYLOAD_FIXTURE_DIR must identify the shared ZIP-payload fixture directory"
#endif

namespace {

int failures = 0;

void check(bool condition, const std::string& label)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", label.c_str());
        ++failures;
    }
}

} // namespace

int main()
{
    try
    {
        const std::vector<flynes::test::ZipPayloadFixture> fixtures =
            flynes::test::load_zip_payload_fixtures(FLYNES_ZIP_PAYLOAD_FIXTURE_DIR);
        check(fixtures.size() == 19u, "payload fixture corpus has exactly 19 cases");
        for (const flynes::test::ZipPayloadFixture& fixture : fixtures)
        {
            const auto limits = flynes::catalog::BoundedZipLimits::create(
                fixture.limits.max_package_bytes,
                fixture.limits.max_payload_bytes,
                fixture.limits.max_zip_entries,
                fixture.limits.max_cumulative_inflated_bytes,
                fixture.limits.max_name_bytes,
                fixture.limits.max_compression_ratio,
                fixture.limits.ratio_guard_threshold_bytes);
            check(limits.has_value(), fixture.case_id + ": fixture limits are valid");
            if (!limits.has_value())
            {
                continue;
            }
            auto opened = flynes::catalog::open_bounded_zip(
                fixture.archive.data(), fixture.archive.size(), *limits);
            check(opened.succeeded(), fixture.case_id + ": archive passes open validation");
            if (!opened.succeeded())
            {
                continue;
            }

            auto result = flynes::catalog::read_bounded_zip_payload(
                *opened.archive(),
                fixture.selector_raw_name,
                fixture.selector_local_header_offset);
            check(result.succeeded() == fixture.succeeds,
                  fixture.case_id + ": payload outcome");
            if (fixture.succeeds)
            {
                const std::vector<std::uint8_t>* const payload = result.payload();
                check(payload != nullptr, fixture.case_id + ": payload exists");
                if (payload != nullptr)
                {
                    check(payload->size() == fixture.payload_length,
                          fixture.case_id + ": payload length");
                    check(flynes::test::fixture_sha256(*payload) == fixture.payload_sha256,
                          fixture.case_id + ": payload SHA-256");
                }
            }
            else
            {
                const flynes::catalog::BoundedZipOpenError* const error = result.error();
                check(error != nullptr, fixture.case_id + ": payload error exists");
                if (error != nullptr)
                {
                    check(flynes::catalog::zip_open_code_name(error->code) == fixture.error_code,
                          fixture.case_id + ": payload error code");
                    check(error->message == fixture.error_message,
                          fixture.case_id + ": payload error message");
                }
            }
        }
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: ZIP-payload fixture harness: %s\n", error.what());
        ++failures;
    }

    if (failures == 0)
    {
        std::puts("flynes_bounded_zip_payload_test: PASS (19 fixtures)");
    }
    return failures == 0 ? 0 : 1;
}
