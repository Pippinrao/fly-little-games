#ifndef FLYNES_TESTS_IDENTITY_FIXTURE_HPP
#define FLYNES_TESTS_IDENTITY_FIXTURE_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace flynes::test {

struct IdentityHashFixture final
{
    std::string case_id;
    std::string blob_name;
    std::vector<std::uint8_t> blob;
    std::string expected_sha1;
    std::string expected_sha256;
    std::string expected_crc32;
};

enum class StableIdOperation : std::uint8_t
{
    PACKAGE_ID = 0,
    SAF_SOURCE_ID = 1,
    VARIANT_ID = 2,
    PROVISIONAL_GAME_ID = 3,
    ENTRY_OUTCOME_ID = 4,
};

struct StableIdFixture final
{
    std::string case_id;
    StableIdOperation operation = StableIdOperation::PACKAGE_ID;
    std::vector<std::string> arguments;
    bool succeeds = false;
    std::string expected_value;
    std::string expected_message;
};

struct IdentityFixtureCorpus final
{
    std::vector<IdentityHashFixture> hashes;
    std::vector<StableIdFixture> stable_ids;
};

IdentityFixtureCorpus load_identity_fixtures(const std::string& fixture_root);

} // namespace flynes::test

#endif
