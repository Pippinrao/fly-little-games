#include "catalog/unsupported_payload_classifier.hpp"
#include "unsupported_payload_fixture.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <vector>

#ifndef FLYNES_UNSUPPORTED_PAYLOAD_FIXTURE_DIR
#error "FLYNES_UNSUPPORTED_PAYLOAD_FIXTURE_DIR must identify the shared fixture directory"
#endif

namespace {

int failures = 0;

void check(bool condition, const char* test_name)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", test_name);
        ++failures;
    }
}

flynes::catalog::UnsupportedPayloadReason expected_reason(
    flynes::test::ExpectedUnsupportedPayloadReason value)
{
    using Expected = flynes::test::ExpectedUnsupportedPayloadReason;
    using Actual = flynes::catalog::UnsupportedPayloadReason;
    switch (value)
    {
    case Expected::NESTED_ARCHIVE:
        return Actual::NESTED_ARCHIVE;
    case Expected::EXECUTABLE:
        return Actual::EXECUTABLE;
    case Expected::GAME_BOY:
        return Actual::GAME_BOY;
    case Expected::SIDECAR:
        return Actual::SIDECAR;
    case Expected::UNKNOWN_FORMAT:
        return Actual::UNKNOWN_FORMAT;
    }
    return Actual::UNKNOWN_FORMAT;
}

void test_fixture_parity()
{
    const std::vector<flynes::test::UnsupportedPayloadFixture> fixtures =
        flynes::test::load_unsupported_payload_fixtures(
            FLYNES_UNSUPPORTED_PAYLOAD_FIXTURE_DIR);
    check(fixtures.size() == 38u, "fixture count");
    for (const flynes::test::UnsupportedPayloadFixture& fixture : fixtures)
    {
        const std::vector<std::uint8_t> before = fixture.blob;
        const flynes::catalog::UnsupportedPayloadReason actual =
            flynes::catalog::classify_unsupported_payload(
                {fixture.blob.data(), fixture.blob.size()});
        check(actual == expected_reason(fixture.expected_reason), fixture.case_id.c_str());
        check(fixture.blob == before, "classifier must not mutate borrowed bytes");
    }
}

void test_invalid_views()
{
    using flynes::catalog::UnsupportedPayloadReason;
    check(flynes::catalog::classify_unsupported_payload({nullptr, 0u}) ==
              UnsupportedPayloadReason::SIDECAR,
          "null empty view is an empty sidecar");
    check(flynes::catalog::classify_unsupported_payload({nullptr, 1u}) ==
              UnsupportedPayloadReason::UNKNOWN_FORMAT,
          "null non-empty view is safely unknown");
}

} // namespace

static_assert(std::is_same_v<std::underlying_type_t<flynes::catalog::UnsupportedPayloadReason>,
                             std::uint8_t>);
static_assert(static_cast<std::uint8_t>(
                  flynes::catalog::UnsupportedPayloadReason::NESTED_ARCHIVE) == 0u);
static_assert(static_cast<std::uint8_t>(
                  flynes::catalog::UnsupportedPayloadReason::EXECUTABLE) == 1u);
static_assert(static_cast<std::uint8_t>(
                  flynes::catalog::UnsupportedPayloadReason::GAME_BOY) == 2u);
static_assert(static_cast<std::uint8_t>(
                  flynes::catalog::UnsupportedPayloadReason::SIDECAR) == 3u);
static_assert(static_cast<std::uint8_t>(
                  flynes::catalog::UnsupportedPayloadReason::UNKNOWN_FORMAT) == 4u);

int main()
{
    test_fixture_parity();
    test_invalid_views();
    if (failures == 0)
    {
        std::puts("flynes_unsupported_payload_classifier_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
