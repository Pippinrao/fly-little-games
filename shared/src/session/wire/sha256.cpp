#include "sha256.hpp"

#include <cstring>
#include <vector>

namespace flynes::session::wire {
namespace {

constexpr std::array<std::uint32_t, 64> kK = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u, 0x3956C25Bu, 0x59F111F1u,
    0x923F82A4u, 0xAB1C5ED5u, 0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u, 0xE49B69C1u, 0xEFBE4786u,
    0x0FC19DC6u, 0x240CA1CCu, 0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u, 0xC6E00BF3u, 0xD5A79147u,
    0x06CA6351u, 0x14292967u, 0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u, 0xA2BFE8A1u, 0xA81A664Bu,
    0xC24B8B70u, 0xC76C51A3u, 0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u, 0x391C0CB3u, 0x4ED8AA4Au,
    0x5B9CCA4Fu, 0x682E6FF3u, 0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u,
};

std::uint32_t rotr(std::uint32_t value, unsigned int bits) noexcept
{
    return (value >> bits) | (value << (32u - bits));
}

void block(std::array<std::uint32_t, 8>& state, const std::uint8_t* data) noexcept
{
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16u; ++i)
    {
        w[i] = (static_cast<std::uint32_t>(data[i * 4u]) << 24u) |
               (static_cast<std::uint32_t>(data[i * 4u + 1u]) << 16u) |
               (static_cast<std::uint32_t>(data[i * 4u + 2u]) << 8u) |
               static_cast<std::uint32_t>(data[i * 4u + 3u]);
    }
    for (std::size_t i = 16u; i < 64u; ++i)
    {
        const std::uint32_t s1 = rotr(w[i - 2u], 17u) ^ rotr(w[i - 2u], 19u) ^ (w[i - 2u] >> 10u);
        const std::uint32_t s0 = rotr(w[i - 15u], 7u) ^ rotr(w[i - 15u], 18u) ^ (w[i - 15u] >> 3u);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (std::size_t i = 0; i < 64u; ++i)
    {
        const std::uint32_t s1 = rotr(e, 6u) ^ rotr(e, 11u) ^ rotr(e, 25u);
        const std::uint32_t t1 = h + s1 + ((e & f) ^ (~e & g)) + kK[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2u) ^ rotr(a, 13u) ^ rotr(a, 22u);
        const std::uint32_t t2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
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

std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t size)
{
    std::array<std::uint32_t, 8> state = {
        0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
        0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u,
    };
    const std::size_t full = size / 64u;
    for (std::size_t i = 0; i < full; ++i)
        block(state, data + i * 64u);
    const std::size_t rem = size - full * 64u;
    std::array<std::uint8_t, 128> tail{};
    if (rem != 0u && data != nullptr)
        std::memcpy(tail.data(), data + full * 64u, rem);
    tail[rem] = 0x80u;
    const std::size_t padded = rem < 56u ? 64u : 128u;
    const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8u;
    for (std::size_t i = 0; i < 8u; ++i)
        tail[padded - 1u - i] = static_cast<std::uint8_t>(bits >> (i * 8u));
    block(state, tail.data());
    if (padded == 128u)
        block(state, tail.data() + 64u);
    std::array<std::uint8_t, 32> out{};
    for (std::size_t i = 0; i < 8u; ++i)
    {
        out[i * 4u] = static_cast<std::uint8_t>(state[i] >> 24u);
        out[i * 4u + 1u] = static_cast<std::uint8_t>(state[i] >> 16u);
        out[i * 4u + 2u] = static_cast<std::uint8_t>(state[i] >> 8u);
        out[i * 4u + 3u] = static_cast<std::uint8_t>(state[i]);
    }
    return out;
}

std::array<std::uint8_t, 32> domain_hash(const char* domain,
                                         const std::uint8_t* data,
                                         std::size_t size)
{
    const std::size_t domain_len = std::strlen(domain);
    std::vector<std::uint8_t> preimage;
    preimage.resize(domain_len + 4u + size);
    std::memcpy(preimage.data(), domain, domain_len);
    preimage[domain_len] = static_cast<std::uint8_t>(size >> 24u);
    preimage[domain_len + 1u] = static_cast<std::uint8_t>(size >> 16u);
    preimage[domain_len + 2u] = static_cast<std::uint8_t>(size >> 8u);
    preimage[domain_len + 3u] = static_cast<std::uint8_t>(size);
    if (size != 0u)
        std::memcpy(preimage.data() + domain_len + 4u, data, size);
    return sha256(preimage.data(), preimage.size());
}

} // namespace flynes::session::wire
