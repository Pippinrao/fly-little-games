#ifndef FLYNES_TESTS_UNSUPPORTED_PAYLOAD_FIXTURE_HPP
#define FLYNES_TESTS_UNSUPPORTED_PAYLOAD_FIXTURE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace flynes::test {

enum class ExpectedUnsupportedPayloadReason : std::uint8_t
{
    NESTED_ARCHIVE = 0,
    EXECUTABLE = 1,
    GAME_BOY = 2,
    SIDECAR = 3,
    UNKNOWN_FORMAT = 4,
};

struct UnsupportedPayloadFixture final
{
    std::string case_id;
    std::string blob_name;
    std::vector<std::uint8_t> blob;
    ExpectedUnsupportedPayloadReason expected_reason =
        ExpectedUnsupportedPayloadReason::UNKNOWN_FORMAT;
};

std::vector<UnsupportedPayloadFixture> load_unsupported_payload_fixtures(
    const std::string& fixture_root);

} // namespace flynes::test

#endif
