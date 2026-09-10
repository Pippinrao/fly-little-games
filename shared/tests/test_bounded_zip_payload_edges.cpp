#include "catalog/bounded_zip_archive.hpp"
#include "zip_payload_fixture.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef FLYNES_ZIP_PAYLOAD_FIXTURE_DIR
#error "FLYNES_ZIP_PAYLOAD_FIXTURE_DIR must identify the shared ZIP-payload fixture directory"
#endif

namespace {

int failures = 0;

void check(bool condition, const char* label)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++failures;
    }
}

const flynes::test::ZipPayloadFixture* find_fixture(
    const std::vector<flynes::test::ZipPayloadFixture>& fixtures,
    const std::string& case_id)
{
    const auto found = std::find_if(
        fixtures.begin(), fixtures.end(), [&](const auto& fixture) {
            return fixture.case_id == case_id;
        });
    return found == fixtures.end() ? nullptr : &*found;
}

} // namespace

static_assert(!std::is_copy_constructible_v<flynes::catalog::BoundedZipPayloadResult>,
              "a payload result must retain one owned payload allocation");
static_assert(std::is_move_constructible_v<flynes::catalog::BoundedZipPayloadResult>,
              "a payload result must be movable");

int main()
{
    const std::vector<flynes::test::ZipPayloadFixture> fixtures =
        flynes::test::load_zip_payload_fixtures(FLYNES_ZIP_PAYLOAD_FIXTURE_DIR);
    const flynes::test::ZipPayloadFixture* const fixture =
        find_fixture(fixtures, "stored_exact_limit_local_extra");
    check(fixture != nullptr, "ownership fixture exists");
    if (fixture != nullptr)
    {
        const auto limits = flynes::catalog::BoundedZipLimits::create(
            fixture->limits.max_package_bytes,
            fixture->limits.max_payload_bytes,
            fixture->limits.max_zip_entries,
            fixture->limits.max_cumulative_inflated_bytes,
            fixture->limits.max_name_bytes,
            fixture->limits.max_compression_ratio,
            fixture->limits.ratio_guard_threshold_bytes);
        check(limits.has_value(), "ownership fixture limits are valid");
        if (limits.has_value())
        {
            std::vector<std::uint8_t> caller_bytes = fixture->archive;
            auto opened = flynes::catalog::open_bounded_zip(
                caller_bytes.data(), caller_bytes.size(), *limits);
            check(opened.succeeded(), "ownership fixture opens");
            caller_bytes.assign(caller_bytes.size(), 0xA5u);

            auto moved = std::move(opened);
            check(moved.archive() != nullptr, "archive remains present after result move");
            if (moved.archive() != nullptr)
            {
                auto first = flynes::catalog::read_bounded_zip_payload(
                    *moved.archive(),
                    fixture->selector_raw_name,
                    fixture->selector_local_header_offset);
                check(first.succeeded(), "caller mutation does not affect owned archive");
                std::vector<std::uint8_t>* const first_payload = first.payload();
                check(first_payload != nullptr && !first_payload->empty(),
                      "first payload is mutable owned storage");
                if (first_payload != nullptr && !first_payload->empty())
                {
                    (*first_payload)[0] ^= 0xFFu;
                }
                auto second = flynes::catalog::read_bounded_zip_payload(
                    *moved.archive(),
                    fixture->selector_raw_name,
                    fixture->selector_local_header_offset);
                check(second.succeeded(), "same selected entry can be read repeatedly");
                check(second.payload() != nullptr &&
                          flynes::test::fixture_sha256(*second.payload()) ==
                              fixture->payload_sha256,
                      "mutating one returned payload does not affect the next read");
            }
        }
    }

    std::vector<flynes::catalog::BoundedZipEntry> synthetic_entries(2u);
    synthetic_entries[0].raw_name = {0x61u};
    synthetic_entries[0].local_header_offset = 7;
    synthetic_entries[1] = synthetic_entries[0];
    const auto duplicate = flynes::catalog::detail::select_exact_bounded_zip_entry(
        synthetic_entries, synthetic_entries[0].raw_name, 7);
    check(duplicate.status == flynes::catalog::detail::ExactEntrySelectionStatus::DUPLICATE,
          "exact selector reports duplicate identity without choosing arbitrarily");
    const auto missing = flynes::catalog::detail::select_exact_bounded_zip_entry(
        synthetic_entries, std::vector<std::uint8_t>{0x62u}, 7);
    check(missing.status == flynes::catalog::detail::ExactEntrySelectionStatus::MISSING,
          "exact selector reports a missing identity safely");

    const auto no_progress = flynes::catalog::detail::classify_inflate_stall(false, false);
    check(no_progress == flynes::catalog::detail::InflateStall::NO_PROGRESS,
          "injected nonterminal step with input and no dictionary is no-progress");
    check(std::string(flynes::catalog::detail::inflate_stall_message(no_progress)) ==
              "ZIP deflate stream made no progress",
          "nonterminal no-progress branch has a stable injected policy seam");
    const auto input_exhausted = flynes::catalog::detail::classify_inflate_stall(false, true);
    check(std::string(flynes::catalog::detail::inflate_stall_message(input_exhausted)) ==
              "ZIP deflate stream ended before completion",
          "input exhaustion shares Java's incomplete-stream message");
    const auto dictionary = flynes::catalog::detail::classify_inflate_stall(true, false);
    check(std::string(flynes::catalog::detail::inflate_stall_message(dictionary)) ==
              "ZIP deflate stream ended before completion",
          "dictionary request shares Java's incomplete-stream message");

    if (failures == 0)
    {
        std::puts("flynes_bounded_zip_payload_edges_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
