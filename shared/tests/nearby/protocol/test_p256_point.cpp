#include "p256_point.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

int failures = 0;
void check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

std::array<std::uint8_t, 65> decode(const char* hex)
{
    const auto nibble = [](char ch) -> std::uint8_t {
        return static_cast<std::uint8_t>(
            ch >= '0' && ch <= '9' ? ch - '0' : ch - 'a' + 10);
    };
    std::array<std::uint8_t, 65> result{};
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::uint8_t>(
            (nibble(hex[i * 2]) << 4u) | nibble(hex[i * 2 + 1]));
    return result;
}

void test_p256_uncompressed_point_validation()
{
    auto generator = decode(
        "046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
        "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5");
    check(flynes::session::wire::validate_p256_uncompressed_point(
              generator.data()),
          "NIST P-256 generator is on curve");
    const auto twice_generator = decode(
        "047cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978"
        "07775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1");
    check(flynes::session::wire::validate_p256_uncompressed_point(
              twice_generator.data()),
          "a second independent NIST P-256 point is on curve");
    generator[64] ^= 1;
    check(!flynes::session::wire::validate_p256_uncompressed_point(
               generator.data()),
          "one-bit coordinate mutation is rejected");
    generator[64] ^= 1;
    generator[0] = 0x02;
    check(!flynes::session::wire::validate_p256_uncompressed_point(
               generator.data()),
          "compressed and noncanonical encodings are rejected");

    std::array<std::uint8_t, 65> infinity{};
    infinity[0] = 0x04;
    check(!flynes::session::wire::validate_p256_uncompressed_point(
               infinity.data()) &&
              !flynes::session::wire::validate_p256_uncompressed_point(nullptr),
          "point at infinity and null pointer are rejected");

    auto out_of_field = decode(
        "04ffffffff00000001000000000000000000000000ffffffffffffffffffffffff"
        "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5");
    check(!flynes::session::wire::validate_p256_uncompressed_point(
               out_of_field.data()),
          "coordinate equal to the field modulus is rejected");
}

} // namespace

int main()
{
    test_p256_uncompressed_point_validation();
    return failures == 0 ? 0 : 1;
}
