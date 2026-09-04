#include "catalog/bounded_zip_archive.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

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

void check_error(const flynes::catalog::BoundedZipOpenResult& result,
                 flynes::catalog::ZipOpenCode code,
                 const char* message,
                 const char* label)
{
    check(!result.succeeded(), label);
    check(result.archive() == nullptr, "failed result has no archive");
    check(result.error() != nullptr, "failed result has an error");
    if (result.error() != nullptr)
    {
        check(result.error()->code == code, "direct error code");
        check(result.error()->message == message, "direct error message");
    }
}

std::vector<std::uint8_t> empty_zip()
{
    return {
        0x50u, 0x4Bu, 0x05u, 0x06u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u,
    };
}

} // namespace

static_assert(!std::is_copy_constructible_v<flynes::catalog::BoundedZipArchive>,
              "an opened archive must not duplicate its physical byte buffer");
static_assert(!std::is_copy_assignable_v<flynes::catalog::BoundedZipArchive>,
              "an opened archive must not duplicate its physical byte buffer");
static_assert(std::is_move_constructible_v<flynes::catalog::BoundedZipArchive>,
              "an opened archive must remain movable");
static_assert(!std::is_copy_constructible_v<flynes::catalog::BoundedZipOpenResult>,
              "an open result must preserve single ownership of its archive");
static_assert(std::is_move_constructible_v<flynes::catalog::BoundedZipOpenResult>,
              "an open result must remain movable");

int main()
{
    using flynes::catalog::BoundedZipLimits;
    using flynes::catalog::ZipOpenCode;

    const BoundedZipLimits defaults = BoundedZipLimits::defaults();
    check(defaults.max_package_bytes() == 8u * 1024u * 1024u, "default package limit");
    check(defaults.max_payload_bytes() == 8u * 1024u * 1024u, "default payload limit");
    check(defaults.max_zip_entries() == 2048u, "default entry limit");
    check(defaults.max_cumulative_inflated_bytes() == 32u * 1024u * 1024u,
          "default cumulative limit");
    check(defaults.max_name_bytes() == 1024u, "default name limit");
    check(defaults.max_compression_ratio() == 200u, "default ratio limit");
    check(defaults.ratio_guard_threshold_bytes() == 1024u * 1024u,
          "default ratio guard");

    constexpr std::uint64_t long_max =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    constexpr std::uint64_t int_max =
        static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max());
    const auto make_limits = [](std::uint64_t package,
                                std::uint64_t payload,
                                std::uint64_t entries,
                                std::uint64_t cumulative,
                                std::uint64_t name,
                                std::uint64_t ratio,
                                std::uint64_t guard) {
        return BoundedZipLimits::create(
            package, payload, entries, cumulative, name, ratio, guard);
    };
    check(make_limits(long_max, long_max, int_max, long_max, 0xFFFFu, int_max, long_max)
              .has_value(),
          "all Java-compatible limit maxima are accepted");
    check(!make_limits(0u, 1u, 1u, 1u, 1u, 1u, 0u).has_value(),
          "zero package limit rejected");
    check(!make_limits(1u, 0u, 1u, 1u, 1u, 1u, 0u).has_value(),
          "zero payload limit rejected");
    check(!make_limits(1u, 1u, 0u, 1u, 1u, 1u, 0u).has_value(),
          "zero entry limit rejected");
    check(!make_limits(1u, 1u, 1u, 0u, 1u, 1u, 0u).has_value(),
          "zero cumulative limit rejected");
    check(!make_limits(1u, 1u, 1u, 1u, 0u, 1u, 0u).has_value(),
          "zero name limit rejected");
    check(!make_limits(1u, 1u, 1u, 1u, 0x10000u, 1u, 0u).has_value(),
          "non-ZIP name limit rejected");
    check(!make_limits(1u, 1u, 1u, 1u, 1u, 0u, 0u).has_value(),
          "zero ratio rejected");
    check(!make_limits(long_max + 1u, 1u, 1u, 1u, 1u, 1u, 0u).has_value(),
          "package limit above Java long rejected");
    check(!make_limits(1u, long_max + 1u, 1u, 1u, 1u, 1u, 0u).has_value(),
          "payload limit above Java long rejected");
    check(!make_limits(1u, 1u, int_max + 1u, 1u, 1u, 1u, 0u).has_value(),
          "entry limit above Java int rejected");
    check(!make_limits(1u, 1u, 1u, long_max + 1u, 1u, 1u, 0u).has_value(),
          "cumulative limit above Java long rejected");
    check(!make_limits(1u, 1u, 1u, 1u, 1u, int_max + 1u, 0u).has_value(),
          "ratio above Java int rejected");
    check(!make_limits(1u, 1u, 1u, 1u, 1u, 1u, long_max + 1u).has_value(),
          "guard above Java long rejected");

    check_error(
        flynes::catalog::open_bounded_zip(nullptr, 1u, defaults),
        ZipOpenCode::INVALID_ZIP,
        "ZIP bytes pointer is null",
        "null pointer with nonzero size rejected safely");
    check_error(
        flynes::catalog::open_bounded_zip(nullptr, 0u, defaults),
        ZipOpenCode::INVALID_ZIP,
        "ZIP end record is missing",
        "null empty view is a safe malformed archive");
    const auto tiny_package = make_limits(1u, 1u, 1u, 1u, 1u, 1u, 0u);
    check(tiny_package.has_value(), "tiny limits valid");
    if (tiny_package.has_value())
    {
        check_error(
            flynes::catalog::open_bounded_zip(nullptr, 2u, *tiny_package),
            ZipOpenCode::PACKAGE_LIMIT_EXCEEDED,
            "ZIP package is over the source limit",
            "package bound precedes null pointer validation");
    }

    std::vector<std::uint8_t> input = empty_zip();
    const std::vector<std::uint8_t> original = input;
    const flynes::catalog::BoundedZipOpenResult opened =
        flynes::catalog::open_bounded_zip(input.data(), input.size(), defaults);
    check(opened.succeeded(), "valid empty archive opens");
    input.assign(input.size(), 0xA5u);
    if (opened.archive() != nullptr)
    {
        check(opened.archive()->physical_bytes() == original,
              "archive owns one independent exact physical copy");
        check(opened.archive()->entries().empty(), "empty archive has no entries");
    }

    if (failures == 0)
    {
        std::puts("flynes_bounded_zip_archive_edges_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
