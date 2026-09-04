#include "identity_fixture.hpp"

#include "catalog/content_identity.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef FLYNES_IDENTITY_FIXTURE_DIR
#error "FLYNES_IDENTITY_FIXTURE_DIR must identify the shared identity fixture directory"
#endif

namespace {

int failures = 0;

void check(bool condition, const std::string& case_id, const std::string& field)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s: %s\n", case_id.c_str(), field.c_str());
        ++failures;
    }
}

flynes::catalog::ContentBytes bytes_of(const std::vector<std::uint8_t>& bytes)
{
    return {bytes.empty() ? nullptr : bytes.data(), bytes.size()};
}

flynes::catalog::StableIdResult evaluate(const flynes::test::StableIdFixture& fixture)
{
    using flynes::test::StableIdOperation;
    switch (fixture.operation)
    {
    case StableIdOperation::PACKAGE_ID:
        return flynes::catalog::package_id(fixture.arguments[0], fixture.arguments[1]);
    case StableIdOperation::SAF_SOURCE_ID:
        return flynes::catalog::saf_source_id(fixture.arguments[0]);
    case StableIdOperation::VARIANT_ID:
        return flynes::catalog::variant_id(
            fixture.arguments[0], fixture.arguments[1], fixture.arguments[2]);
    case StableIdOperation::PROVISIONAL_GAME_ID:
        return flynes::catalog::provisional_game_id(fixture.arguments[0]);
    case StableIdOperation::ENTRY_OUTCOME_ID:
        return flynes::catalog::entry_outcome_id(
            fixture.arguments[0], fixture.arguments[1]);
    }
    return {};
}

void check_hash_fixture(const flynes::test::IdentityHashFixture& fixture)
{
    const std::vector<std::uint8_t> original = fixture.blob;
    const flynes::catalog::RomContentHashResult result =
        flynes::catalog::hash_rom_content(bytes_of(fixture.blob), bytes_of(fixture.blob));
    check(result.ok(), fixture.case_id, "hash calculation succeeds");
    if (!result.ok())
    {
        return;
    }
    check(result.value.payload_sha1 == fixture.expected_sha1, fixture.case_id, "SHA-1");
    check(result.value.payload_sha256 == fixture.expected_sha256, fixture.case_id, "SHA-256");
    check(result.value.physical_package_sha256 == fixture.expected_sha256,
          fixture.case_id,
          "physical SHA-256");
    check(result.value.crc32 == fixture.expected_crc32, fixture.case_id, "CRC32");
    check(fixture.blob == original, fixture.case_id, "borrowed input remains unchanged");

    flynes::catalog::Sha1Hasher sha1;
    flynes::catalog::Sha256Hasher sha256;
    flynes::catalog::Crc32Hasher crc32;
    std::size_t offset = 0u;
    const std::size_t chunks[] = {1u, 7u, 64u, 3u, 129u};
    std::size_t chunk_index = 0u;
    while (offset < fixture.blob.size())
    {
        const std::size_t count = std::min(
            chunks[chunk_index % (sizeof(chunks) / sizeof(chunks[0]))],
            fixture.blob.size() - offset);
        const flynes::catalog::ContentBytes chunk = {fixture.blob.data() + offset, count};
        check(sha1.update(chunk) == flynes::catalog::ContentIdentityError::NONE,
              fixture.case_id,
              "streaming SHA-1 update");
        check(sha256.update(chunk) == flynes::catalog::ContentIdentityError::NONE,
              fixture.case_id,
              "streaming SHA-256 update");
        check(crc32.update(chunk) == flynes::catalog::ContentIdentityError::NONE,
              fixture.case_id,
              "streaming CRC32 update");
        offset += count;
        ++chunk_index;
    }
    check(sha1.finish_hex().value == fixture.expected_sha1,
          fixture.case_id,
          "streaming SHA-1 finish");
    check(sha256.finish_hex().value == fixture.expected_sha256,
          fixture.case_id,
          "streaming SHA-256 finish");
    check(crc32.finish_hex().value == fixture.expected_crc32,
          fixture.case_id,
          "streaming CRC32 finish");
}

void test_hash_contracts(const flynes::test::IdentityFixtureCorpus& corpus)
{
    std::unordered_map<std::string, const flynes::test::IdentityHashFixture*> by_id;
    for (const auto& fixture : corpus.hashes)
    {
        by_id.emplace(fixture.case_id, &fixture);
        check_hash_fixture(fixture);
    }

    check(corpus.hashes.size() == 12u, "hash_case_set", "exact fixture count");
    const std::vector<std::string> required = {
        "empty", "abc", "binary_00_ff", "pad_55", "pad_56", "pad_63", "pad_64",
        "pad_65", "million_a", "digits_123456789", "ownership_payload",
        "ownership_physical",
    };
    for (const std::string& case_id : required)
    {
        check(by_id.count(case_id) == 1u, case_id, "required hash fixture");
    }

    const auto* payload = by_id.at("ownership_payload");
    const auto* physical = by_id.at("ownership_physical");
    std::vector<std::uint8_t> payload_copy = payload->blob;
    std::vector<std::uint8_t> physical_copy = physical->blob;
    const flynes::catalog::RomContentHashResult separated = flynes::catalog::hash_rom_content(
        bytes_of(payload_copy), bytes_of(physical_copy));
    payload_copy[0] ^= 0x7Fu;
    physical_copy[0] ^= 0x7Fu;
    check(separated.ok(), "ownership_pair", "hash succeeds");
    check(separated.value.payload_sha1 == payload->expected_sha1,
          "ownership_pair",
          "payload SHA-1 is owned");
    check(separated.value.payload_sha256 == payload->expected_sha256,
          "ownership_pair",
          "payload SHA-256 is owned");
    check(separated.value.crc32 == payload->expected_crc32,
          "ownership_pair",
          "payload CRC32 is owned");
    check(separated.value.physical_package_sha256 == physical->expected_sha256,
          "ownership_pair",
          "physical SHA-256 is distinct");

    const std::uint8_t sentinel = 0u;
    const auto invalid = flynes::catalog::sha256_hex({nullptr, 1u});
    check(invalid.error == flynes::catalog::ContentIdentityError::INVALID_BYTE_VIEW,
          "invalid_byte_view",
          "null is rejected when length is nonzero");
    check(flynes::catalog::sha256_hex({nullptr, 0u}).ok(),
          "empty_byte_view",
          "null is accepted when length is zero");
    check(flynes::catalog::validate_hash_byte_count(
              std::numeric_limits<std::uint64_t>::max() / 8u, 1u) ==
              flynes::catalog::ContentIdentityError::LENGTH_OVERFLOW,
          "hash_length_overflow",
          "bit-length overflow is rejected without a fabricated byte range");

    flynes::catalog::Sha256Hasher owned_stream;
    std::vector<std::uint8_t> mutable_bytes = {'a', 'b', 'c'};
    check(owned_stream.update(bytes_of(mutable_bytes)) ==
              flynes::catalog::ContentIdentityError::NONE,
          "stream_ownership",
          "update succeeds");
    mutable_bytes.assign({'x', 'y', 'z'});
    const auto first_finish = owned_stream.finish_hex();
    check(first_finish.value ==
              "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
          "stream_ownership",
          "update consumes borrowed bytes synchronously");
    check(owned_stream.finish_hex().value == first_finish.value,
          "stream_ownership",
          "finish is stable and owned");
    check(owned_stream.update({&sentinel, 1u}) ==
              flynes::catalog::ContentIdentityError::ALREADY_FINALIZED,
          "stream_ownership",
          "update after finish is rejected");
}

void test_stable_ids(const flynes::test::IdentityFixtureCorpus& corpus)
{
    std::unordered_set<std::string> ids;
    for (const flynes::test::StableIdFixture& fixture : corpus.stable_ids)
    {
        ids.insert(fixture.case_id);
        const flynes::catalog::StableIdResult result = evaluate(fixture);
        check(result.ok() == fixture.succeeds, fixture.case_id, "success/error outcome");
        if (fixture.succeeds)
        {
            check(result.value == fixture.expected_value, fixture.case_id, "stable ID value");
            check(result.message.empty(), fixture.case_id, "success message is empty");
        }
        else
        {
            check(result.value.empty(), fixture.case_id, "error value is empty");
            check(result.message == fixture.expected_message,
                  fixture.case_id,
                  "stable ID error message");
        }
    }
    check(corpus.stable_ids.size() == 38u, "stable_case_set", "exact fixture count");
    check(ids.size() == corpus.stable_ids.size(), "stable_case_set", "unique fixture IDs");

    const auto framing_a = flynes::catalog::package_id("ab", "c");
    const auto framing_b = flynes::catalog::package_id("a", "bc");
    check(framing_a.ok() && framing_b.ok() && framing_a.value != framing_b.value,
          "stable_framing",
          "BE32 framing distinguishes concatenations");
    const auto nfc = flynes::catalog::entry_outcome_id("pkg:caf\xC3\xA9", "RAW");
    const auto nfd = flynes::catalog::entry_outcome_id("pkg:cafe\xCC\x81", "RAW");
    check(nfc.ok() && nfd.ok() && nfc.value != nfd.value,
          "stable_normalization",
          "NFC and NFD remain distinct");
    const std::string with_nul("a\0b", 3u);
    check(flynes::catalog::package_id(with_nul, "document").value !=
              flynes::catalog::package_id("ab", "document").value,
          "stable_nul",
          "embedded NUL participates in framing and hash");

    check(flynes::catalog::validate_id_utf8_length(
              static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) + 1u) ==
              flynes::catalog::ContentIdentityError::LENGTH_OVERFLOW,
          "stable_length_overflow",
          "input larger than INT32_MAX is rejected via checked length seam");
}

} // namespace

int main()
{
    try
    {
        const flynes::test::IdentityFixtureCorpus corpus =
            flynes::test::load_identity_fixtures(FLYNES_IDENTITY_FIXTURE_DIR);
        test_hash_contracts(corpus);
        test_stable_ids(corpus);
    }
    catch (const std::exception& failure)
    {
        std::fprintf(stderr, "FAIL: content identity fixture harness: %s\n", failure.what());
        ++failures;
    }

    if (failures == 0)
    {
        std::puts("flynes_content_identity_test: PASS (12 hash, 38 stable-ID fixtures)");
    }
    return failures == 0 ? 0 : 1;
}
