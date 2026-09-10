#include "fixture_sha256.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

namespace flynes::ios {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u,
};

std::uint32_t rotate_right(std::uint32_t value, unsigned int count) noexcept
{
    return (value >> count) | (value << (32u - count));
}

std::uint32_t read_big_endian_u32(const std::uint8_t* bytes) noexcept
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24u) |
           (static_cast<std::uint32_t>(bytes[1]) << 16u) |
           (static_cast<std::uint32_t>(bytes[2]) << 8u) |
           static_cast<std::uint32_t>(bytes[3]);
}

void process_block(std::array<std::uint32_t, 8>& state,
                   const std::uint8_t* block) noexcept
{
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0u; index < 16u; ++index)
        words[index] = read_big_endian_u32(block + index * 4u);
    for (std::size_t index = 16u; index < words.size(); ++index)
    {
        const std::uint32_t previous_two = words[index - 2u];
        const std::uint32_t previous_fifteen = words[index - 15u];
        const std::uint32_t sigma_one =
            rotate_right(previous_two, 17u) ^ rotate_right(previous_two, 19u) ^
            (previous_two >> 10u);
        const std::uint32_t sigma_zero =
            rotate_right(previous_fifteen, 7u) ^ rotate_right(previous_fifteen, 18u) ^
            (previous_fifteen >> 3u);
        words[index] =
            words[index - 16u] + sigma_zero + words[index - 7u] + sigma_one;
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];
    for (std::size_t index = 0u; index < words.size(); ++index)
    {
        const std::uint32_t sum_one =
            rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t temporary_one =
            h + sum_one + choice + kRoundConstants[index] + words[index];
        const std::uint32_t sum_zero =
            rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temporary_two = sum_zero + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary_one;
        d = c;
        c = b;
        b = a;
        a = temporary_one + temporary_two;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

} // namespace

std::string fixture_sha256_hex(const std::uint8_t* data, std::size_t size)
{
    if ((data == nullptr && size != 0u) ||
        size > std::numeric_limits<std::uint64_t>::max() / 8u)
        return {};

    std::array<std::uint32_t, 8> state = {
        0x6A09E667u,
        0xBB67AE85u,
        0x3C6EF372u,
        0xA54FF53Au,
        0x510E527Fu,
        0x9B05688Cu,
        0x1F83D9ABu,
        0x5BE0CD19u,
    };

    const std::size_t full_block_count = size / 64u;
    for (std::size_t index = 0u; index < full_block_count; ++index)
        process_block(state, data + index * 64u);

    const std::size_t remaining = size - full_block_count * 64u;
    std::array<std::uint8_t, 128> tail{};
    if (remaining != 0u)
        std::memcpy(tail.data(), data + full_block_count * 64u, remaining);
    tail[remaining] = 0x80u;
    const std::size_t padded_size = remaining < 56u ? 64u : 128u;
    const std::uint64_t bit_length = static_cast<std::uint64_t>(size) * 8u;
    for (std::size_t index = 0u; index < 8u; ++index)
    {
        tail[padded_size - 1u - index] =
            static_cast<std::uint8_t>(bit_length >> (index * 8u));
    }
    process_block(state, tail.data());
    if (padded_size == tail.size())
        process_block(state, tail.data() + 64u);

    constexpr char uppercase_hex[] = "0123456789ABCDEF";
    std::string result(64u, '0');
    for (std::size_t word_index = 0u; word_index < state.size(); ++word_index)
    {
        for (std::size_t byte_index = 0u; byte_index < 4u; ++byte_index)
        {
            const unsigned int shift = static_cast<unsigned int>((3u - byte_index) * 8u);
            const std::uint8_t byte = static_cast<std::uint8_t>(state[word_index] >> shift);
            const std::size_t output_index = word_index * 8u + byte_index * 2u;
            result[output_index] = uppercase_hex[byte >> 4u];
            result[output_index + 1u] = uppercase_hex[byte & 0x0Fu];
        }
    }
    return result;
}

} // namespace flynes::ios
