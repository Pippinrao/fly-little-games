#include "p256_point.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::session::wire {
namespace {

using Field = std::array<std::uint32_t, 8>;

constexpr Field kPrime{{
    UINT32_C(0xffffffff), UINT32_C(0xffffffff), UINT32_C(0xffffffff),
    UINT32_C(0x00000000), UINT32_C(0x00000000), UINT32_C(0x00000000),
    UINT32_C(0x00000001), UINT32_C(0xffffffff)}};

constexpr Field kB{{
    UINT32_C(0x27d2604b), UINT32_C(0x3bce3c3e), UINT32_C(0xcc53b0f6),
    UINT32_C(0x651d06b0), UINT32_C(0x769886bc), UINT32_C(0xb3ebbd55),
    UINT32_C(0xaa3a93e7), UINT32_C(0x5ac635d8)}};

Field parse_be(const std::uint8_t* bytes) noexcept
{
    Field value{};
    for (std::size_t limb = 0; limb < value.size(); ++limb)
    {
        const auto offset = 28 - limb * 4;
        value[limb] =
            (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 16u) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 8u) |
            bytes[offset + 3];
    }
    return value;
}

int compare(const Field& left, const Field& right) noexcept
{
    for (std::size_t index = left.size(); index-- > 0;)
    {
        if (left[index] != right[index])
            return left[index] < right[index] ? -1 : 1;
    }
    return 0;
}

Field subtract(const Field& left, const Field& right) noexcept
{
    Field value{};
    std::uint64_t borrow = 0;
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        const std::uint64_t subtrahend =
            static_cast<std::uint64_t>(right[index]) + borrow;
        value[index] = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(left[index]) - subtrahend);
        borrow = static_cast<std::uint64_t>(left[index]) < subtrahend ? 1 : 0;
    }
    return value;
}

Field add_mod(const Field& left, const Field& right) noexcept
{
    Field value{};
    std::uint64_t carry = 0;
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        const std::uint64_t sum = static_cast<std::uint64_t>(left[index]) +
                                  right[index] + carry;
        value[index] = static_cast<std::uint32_t>(sum);
        carry = sum >> 32u;
    }
    if (carry == 0 && compare(value, kPrime) < 0)
        return value;

    std::uint64_t borrow = 0;
    for (std::size_t index = 0; index < value.size(); ++index)
    {
        const std::uint64_t subtrahend =
            static_cast<std::uint64_t>(kPrime[index]) + borrow;
        const auto current = static_cast<std::uint64_t>(value[index]);
        value[index] = static_cast<std::uint32_t>(current - subtrahend);
        borrow = current < subtrahend ? 1 : 0;
    }
    return value;
}

Field sub_mod(const Field& left, const Field& right) noexcept
{
    if (compare(left, right) >= 0)
        return subtract(left, right);
    return subtract(kPrime, subtract(right, left));
}

Field multiply_mod(const Field& left, const Field& right) noexcept
{
    Field result{};
    Field addend = left;
    for (std::size_t bit = 0; bit < 256; ++bit)
    {
        if ((right[bit / 32] & (UINT32_C(1) << (bit % 32))) != 0)
            result = add_mod(result, addend);
        if (bit != 255)
            addend = add_mod(addend, addend);
    }
    return result;
}

} // namespace

bool validate_p256_uncompressed_point(
    const std::uint8_t point_x963[65]) noexcept
{
    if (point_x963 == nullptr || point_x963[0] != 0x04)
        return false;
    const auto x = parse_be(point_x963 + 1);
    const auto y = parse_be(point_x963 + 33);
    if (compare(x, kPrime) >= 0 || compare(y, kPrime) >= 0)
        return false;

    const auto y_squared = multiply_mod(y, y);
    const auto x_squared = multiply_mod(x, x);
    const auto x_cubed = multiply_mod(x_squared, x);
    const auto two_x = add_mod(x, x);
    const auto three_x = add_mod(two_x, x);
    const auto right = add_mod(sub_mod(x_cubed, three_x), kB);
    return compare(y_squared, right) == 0;
}

bool validate_p256_uncompressed_point_callback(
    void*, const std::uint8_t point_x963[65]) noexcept
{
    return validate_p256_uncompressed_point(point_x963);
}

} // namespace flynes::session::wire
