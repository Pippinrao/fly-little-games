/*
 * W0 two-engine loopback harness — step 2: the shared deterministic world.
 *
 * See the section comment in two_engine_loopback_fixture.hpp. Everything here is a
 * pure function of its inputs (plus one per-world counter for `random`), so two
 * engines sharing one world compute matching pair state without exchanging these
 * values. None of it is cryptography and none of it certifies cryptography.
 */

#include "two_engine_loopback_fixture.hpp"

#include <algorithm>
#include <cstring>

namespace flynes::session::loopback {

int failures = 0;

namespace {

/*
 * k*G for k = 1..12, derived with plain big-integer curve arithmetic by
 * out/logs/gen_p256_points.py and injected here by out/logs/inject_p256_table.py
 * so the bytes are never transcribed by hand. The fixture test re-validates every
 * entry with the repository's own wire::validate_p256_uncompressed_point and pins
 * k = 1 and k = 2 against the constants the repository already ships.
 */
const std::array<LoopbackPoint, kLoopbackPointCount> kPoints{{
    {{
      0x04, 0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42, 0x47, 0xf8, 0xbc, 0xe6,
      0xe5, 0x63, 0xa4, 0x40, 0xf2, 0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33,
      0xa0, 0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2, 0x96, 0x4f, 0xe3, 0x42,
      0xe2, 0xfe, 0x1a, 0x7f, 0x9b, 0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e,
      0x16, 0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e, 0xce, 0xcb, 0xb6, 0x40,
      0x68, 0x37, 0xbf, 0x51, 0xf5}},
    {{
      0x04, 0x7c, 0xf2, 0x7b, 0x18, 0x8d, 0x03, 0x4f, 0x7e, 0x8a, 0x52, 0x38,
      0x03, 0x04, 0xb5, 0x1a, 0xc3, 0xc0, 0x89, 0x69, 0xe2, 0x77, 0xf2, 0x1b,
      0x35, 0xa6, 0x0b, 0x48, 0xfc, 0x47, 0x66, 0x99, 0x78, 0x07, 0x77, 0x55,
      0x10, 0xdb, 0x8e, 0xd0, 0x40, 0x29, 0x3d, 0x9a, 0xc6, 0x9f, 0x74, 0x30,
      0xdb, 0xba, 0x7d, 0xad, 0xe6, 0x3c, 0xe9, 0x82, 0x29, 0x9e, 0x04, 0xb7,
      0x9d, 0x22, 0x78, 0x73, 0xd1}},
    {{
      0x04, 0x5e, 0xcb, 0xe4, 0xd1, 0xa6, 0x33, 0x0a, 0x44, 0xc8, 0xf7, 0xef,
      0x95, 0x1d, 0x4b, 0xf1, 0x65, 0xe6, 0xc6, 0xb7, 0x21, 0xef, 0xad, 0xa9,
      0x85, 0xfb, 0x41, 0x66, 0x1b, 0xc6, 0xe7, 0xfd, 0x6c, 0x87, 0x34, 0x64,
      0x0c, 0x49, 0x98, 0xff, 0x7e, 0x37, 0x4b, 0x06, 0xce, 0x1a, 0x64, 0xa2,
      0xec, 0xd8, 0x2a, 0xb0, 0x36, 0x38, 0x4f, 0xb8, 0x3d, 0x9a, 0x79, 0xb1,
      0x27, 0xa2, 0x7d, 0x50, 0x32}},
    {{
      0x04, 0xe2, 0x53, 0x4a, 0x35, 0x32, 0xd0, 0x8f, 0xbb, 0xa0, 0x2d, 0xde,
      0x65, 0x9e, 0xe6, 0x2b, 0xd0, 0x03, 0x1f, 0xe2, 0xdb, 0x78, 0x55, 0x96,
      0xef, 0x50, 0x93, 0x02, 0x44, 0x6b, 0x03, 0x08, 0x52, 0xe0, 0xf1, 0x57,
      0x5a, 0x4c, 0x63, 0x3c, 0xc7, 0x19, 0xdf, 0xee, 0x5f, 0xda, 0x86, 0x2d,
      0x76, 0x4e, 0xfc, 0x96, 0xc3, 0xf3, 0x0e, 0xe0, 0x05, 0x5c, 0x42, 0xc2,
      0x3f, 0x18, 0x4e, 0xd8, 0xc6}},
    {{
      0x04, 0x51, 0x59, 0x0b, 0x7a, 0x51, 0x51, 0x40, 0xd2, 0xd7, 0x84, 0xc8,
      0x56, 0x08, 0x66, 0x8f, 0xdf, 0xef, 0x8c, 0x82, 0xfd, 0x1f, 0x5b, 0xe5,
      0x24, 0x21, 0x55, 0x4a, 0x0d, 0xc3, 0xd0, 0x33, 0xed, 0xe0, 0xc1, 0x7d,
      0xa8, 0x90, 0x4a, 0x72, 0x7d, 0x8a, 0xe1, 0xbf, 0x36, 0xbf, 0x8a, 0x79,
      0x26, 0x0d, 0x01, 0x2f, 0x00, 0xd4, 0xd8, 0x08, 0x88, 0xd1, 0xd0, 0xbb,
      0x44, 0xfd, 0xa1, 0x6d, 0xa4}},
    {{
      0x04, 0xb0, 0x1a, 0x17, 0x2a, 0x76, 0xa4, 0x60, 0x2c, 0x92, 0xd3, 0x24,
      0x2c, 0xb8, 0x97, 0xdd, 0xe3, 0x02, 0x4c, 0x74, 0x0d, 0xeb, 0xb2, 0x15,
      0xb4, 0xc6, 0xb0, 0xaa, 0xe9, 0x3c, 0x22, 0x91, 0xa9, 0xe8, 0x5c, 0x10,
      0x74, 0x32, 0x37, 0xda, 0xd5, 0x6f, 0xec, 0x0e, 0x2d, 0xfb, 0xa7, 0x03,
      0x79, 0x1c, 0x00, 0xf7, 0x70, 0x1c, 0x7e, 0x16, 0xbd, 0xfd, 0x7c, 0x48,
      0x53, 0x8f, 0xc7, 0x7f, 0xe2}},
    {{
      0x04, 0x8e, 0x53, 0x3b, 0x6f, 0xa0, 0xbf, 0x7b, 0x46, 0x25, 0xbb, 0x30,
      0x66, 0x7c, 0x01, 0xfb, 0x60, 0x7e, 0xf9, 0xf8, 0xb8, 0xa8, 0x0f, 0xef,
      0x5b, 0x30, 0x06, 0x28, 0x70, 0x31, 0x87, 0xb2, 0xa3, 0x73, 0xeb, 0x1d,
      0xbd, 0xe0, 0x33, 0x18, 0x36, 0x6d, 0x06, 0x9f, 0x83, 0xa6, 0xf5, 0x90,
      0x00, 0x53, 0xc7, 0x36, 0x33, 0xcb, 0x04, 0x1b, 0x21, 0xc5, 0x5e, 0x1a,
      0x86, 0xc1, 0xf4, 0x00, 0xb4}},
    {{
      0x04, 0x62, 0xd9, 0x77, 0x9d, 0xbe, 0xe9, 0xb0, 0x53, 0x40, 0x42, 0x74,
      0x2d, 0x3a, 0xb5, 0x4c, 0xad, 0xc1, 0xd2, 0x38, 0x98, 0x0f, 0xce, 0x97,
      0xdb, 0xb4, 0xdd, 0x9d, 0xc1, 0xdb, 0x6f, 0xb3, 0x93, 0xad, 0x5a, 0xcc,
      0xbd, 0x91, 0xe9, 0xd8, 0x24, 0x4f, 0xf1, 0x5d, 0x77, 0x11, 0x67, 0xce,
      0xe0, 0xa2, 0xed, 0x51, 0xf6, 0xbb, 0xe7, 0x6a, 0x78, 0xda, 0x54, 0x0a,
      0x6a, 0x0f, 0x09, 0x95, 0x7e}},
    {{
      0x04, 0xea, 0x68, 0xd7, 0xb6, 0xfe, 0xdf, 0x0b, 0x71, 0x87, 0x89, 0x38,
      0xd5, 0x1d, 0x71, 0xf8, 0x72, 0x9e, 0x0a, 0xcb, 0x8c, 0x2c, 0x6d, 0xf8,
      0xb3, 0xd7, 0x9e, 0x8a, 0x4b, 0x90, 0x94, 0x9e, 0xe0, 0x2a, 0x27, 0x44,
      0xc9, 0x72, 0xc9, 0xfc, 0xe7, 0x87, 0x01, 0x4a, 0x96, 0x4a, 0x8e, 0xa0,
      0xc8, 0x4d, 0x71, 0x4f, 0xea, 0xa4, 0xde, 0x82, 0x3f, 0xe8, 0x5a, 0x22,
      0x4a, 0x4d, 0xd0, 0x48, 0xfa}},
    {{
      0x04, 0xce, 0xf6, 0x6d, 0x6b, 0x2a, 0x3a, 0x99, 0x3e, 0x59, 0x12, 0x14,
      0xd1, 0xea, 0x22, 0x3f, 0xb5, 0x45, 0xca, 0x6c, 0x47, 0x1c, 0x48, 0x30,
      0x6e, 0x4c, 0x36, 0x06, 0x94, 0x04, 0xc5, 0x72, 0x3f, 0x87, 0x86, 0x62,
      0xa2, 0x29, 0xaa, 0xae, 0x90, 0x6e, 0x12, 0x3c, 0xdd, 0x9d, 0x3b, 0x4c,
      0x10, 0x59, 0x0d, 0xed, 0x29, 0xfe, 0x75, 0x1e, 0xee, 0xca, 0x34, 0xbb,
      0xaa, 0x44, 0xaf, 0x07, 0x73}},
    {{
      0x04, 0x3e, 0xd1, 0x13, 0xb7, 0x88, 0x3b, 0x4c, 0x59, 0x06, 0x38, 0x37,
      0x9d, 0xb0, 0xc2, 0x1c, 0xda, 0x16, 0x74, 0x2e, 0xd0, 0x25, 0x50, 0x48,
      0xbf, 0x43, 0x33, 0x91, 0xd3, 0x74, 0xbc, 0x21, 0xd1, 0x90, 0x99, 0x20,
      0x9a, 0xcc, 0xc4, 0xc8, 0xa2, 0x24, 0xc8, 0x43, 0xaf, 0xa4, 0xf4, 0xc6,
      0x8a, 0x09, 0x0d, 0x04, 0xda, 0x5e, 0x98, 0x89, 0xda, 0xe2, 0xf8, 0xee,
      0xfc, 0xe8, 0x2a, 0x37, 0x40}},
    {{
      0x04, 0x74, 0x1d, 0xd5, 0xbd, 0xa8, 0x17, 0xd9, 0x5e, 0x46, 0x26, 0x53,
      0x73, 0x20, 0xe5, 0xd5, 0x51, 0x79, 0x98, 0x30, 0x28, 0xb2, 0xf8, 0x2c,
      0x99, 0xd5, 0x00, 0xc5, 0xee, 0x86, 0x24, 0xe3, 0xc4, 0x07, 0x70, 0xb4,
      0x6a, 0x9c, 0x38, 0x5f, 0xdc, 0x56, 0x73, 0x83, 0x55, 0x48, 0x87, 0xb1,
      0x54, 0x8e, 0xeb, 0x91, 0x2c, 0x35, 0xba, 0x5c, 0xa7, 0x19, 0x95, 0xff,
      0x22, 0xcd, 0x44, 0x81, 0xd3}},
}};

} // namespace

const std::array<LoopbackPoint, kLoopbackPointCount>&
loopback_p256_points() noexcept
{
    return kPoints;
}

std::array<std::uint8_t, 32> loopback_hmac_sha256(
    const std::uint8_t* key, std::size_t key_size, const std::uint8_t* input,
    std::size_t input_size) noexcept
{
    /* RFC 2104 HMAC over the repository's sha256, with the standard 64-byte block
     * size. A key longer than the block is hashed first, as the construction
     * requires. */
    std::array<std::uint8_t, 64> block{};
    if (key_size > block.size())
    {
        const auto digest = wire::sha256(key, key_size);
        std::copy(digest.begin(), digest.end(), block.begin());
    }
    else if (key != nullptr && key_size != 0)
    {
        std::memcpy(block.data(), key, key_size);
    }

    std::vector<std::uint8_t> inner;
    inner.reserve(64 + input_size);
    for (std::uint8_t value : block)
        inner.push_back(static_cast<std::uint8_t>(value ^ 0x36u));
    if (input != nullptr && input_size != 0)
        inner.insert(inner.end(), input, input + input_size);
    const auto inner_hash = wire::sha256(inner.data(), inner.size());

    std::array<std::uint8_t, 64 + 32> outer{};
    for (std::size_t index = 0; index < block.size(); ++index)
        outer[index] = static_cast<std::uint8_t>(block[index] ^ 0x5cu);
    std::copy(inner_hash.begin(), inner_hash.end(), outer.begin() + 64);
    return wire::sha256(outer.data(), outer.size());
}

fly_session_resource_handle_v2 LoopbackWorld::allocate_point(
    std::size_t point_index)
{
    Entry entry{};
    entry.handle = next_handle++;
    entry.has_point = true;
    entry.point = kPoints[point_index % kPoints.size()];
    entries.push_back(std::move(entry));
    return entries.back().handle;
}

fly_session_resource_handle_v2 LoopbackWorld::allocate_secret(
    const std::vector<std::uint8_t>& secret)
{
    Entry entry{};
    entry.handle = next_handle++;
    entry.secret = secret;
    entries.push_back(std::move(entry));
    return entries.back().handle;
}

void LoopbackWorld::release(fly_session_resource_handle_v2 handle) noexcept
{
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [handle](const Entry& entry) {
                                     return entry.handle == handle;
                                 }),
                  entries.end());
}

const LoopbackWorld::Entry* LoopbackWorld::find(
    fly_session_resource_handle_v2 handle) const noexcept
{
    for (const Entry& entry : entries)
        if (entry.handle == handle) return &entry;
    return nullptr;
}

const LoopbackPoint* LoopbackWorld::point_of(
    fly_session_resource_handle_v2 handle) const noexcept
{
    const Entry* entry = find(handle);
    return entry != nullptr && entry->has_point ? &entry->point : nullptr;
}

const std::vector<std::uint8_t>* LoopbackWorld::secret_of(
    fly_session_resource_handle_v2 handle) const noexcept
{
    const Entry* entry = find(handle);
    return entry != nullptr && !entry->secret.empty() ? &entry->secret
                                                      : nullptr;
}

std::vector<std::uint8_t> LoopbackWorld::agree(const LoopbackPoint& left,
                                               const LoopbackPoint& right) const
{
    /* ECDH STAND-IN, NOT ECDH — see the header. Ordered so both sides compute the
     * same value no matter which engine is which. */
    const bool left_first = std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end());
    const LoopbackPoint& first = left_first ? left : right;
    const LoopbackPoint& second = left_first ? right : left;

    std::vector<std::uint8_t> input;
    static constexpr char domain[] = "flynes-loopback-dh-v1";
    input.insert(input.end(), domain, domain + sizeof(domain) - 1);
    input.insert(input.end(), first.begin(), first.end());
    input.insert(input.end(), second.begin(), second.end());
    const auto digest = wire::sha256(input.data(), input.size());
    return std::vector<std::uint8_t>(digest.begin(), digest.end());
}

std::vector<std::uint8_t> LoopbackWorld::hkdf(
    const std::vector<std::uint8_t>& secret, const std::uint8_t* salt,
    std::size_t salt_size, const std::uint8_t* info, std::size_t info_size,
    std::size_t size) const
{
    std::vector<std::uint8_t> out;
    out.reserve(size);
    std::uint32_t block_index = 0;
    while (out.size() < size)
    {
        std::vector<std::uint8_t> input;
        static constexpr char domain[] = "flynes-loopback-hkdf-v1";
        input.insert(input.end(), domain, domain + sizeof(domain) - 1);
        input.push_back(static_cast<std::uint8_t>(block_index >> 24u));
        input.push_back(static_cast<std::uint8_t>(block_index >> 16u));
        input.push_back(static_cast<std::uint8_t>(block_index >> 8u));
        input.push_back(static_cast<std::uint8_t>(block_index));
        if (salt != nullptr && salt_size != 0)
            input.insert(input.end(), salt, salt + salt_size);
        if (info != nullptr && info_size != 0)
            input.insert(input.end(), info, info + info_size);
        input.insert(input.end(), secret.begin(), secret.end());
        const auto digest = wire::sha256(input.data(), input.size());
        out.insert(out.end(), digest.begin(), digest.end());
        ++block_index;
    }
    out.resize(size);
    return out;
}

std::array<std::uint8_t, 32> LoopbackWorld::hmac(
    const std::vector<std::uint8_t>& key, const std::uint8_t* input,
    std::size_t input_size) const
{
    return loopback_hmac_sha256(key.data(), key.size(), input, input_size);
}

std::vector<std::uint8_t> LoopbackWorld::seal(
    const std::vector<std::uint8_t>& key, const std::uint8_t* nonce,
    std::size_t nonce_size, const std::uint8_t* aad, std::size_t aad_size,
    const std::uint8_t* input, std::size_t input_size) const
{
    /* Faithful round trip with the tag bound to (key, nonce, aad, plaintext), so a
     * wrong key or a touched ciphertext is detected on open instead of silently
     * producing garbage. */
    std::vector<std::uint8_t> preimage;
    static constexpr char domain[] = "flynes-loopback-aead-v1";
    preimage.insert(preimage.end(), domain, domain + sizeof(domain) - 1);
    if (nonce != nullptr && nonce_size != 0)
        preimage.insert(preimage.end(), nonce, nonce + nonce_size);
    if (aad != nullptr && aad_size != 0)
        preimage.insert(preimage.end(), aad, aad + aad_size);
    if (input != nullptr && input_size != 0)
        preimage.insert(preimage.end(), input, input + input_size);
    const auto tag = hmac(key, preimage.data(), preimage.size());

    std::vector<std::uint8_t> out;
    out.reserve(input_size + 16);
    if (input != nullptr && input_size != 0)
        out.insert(out.end(), input, input + input_size);
    out.insert(out.end(), tag.begin(), tag.begin() + 16);
    return out;
}

bool LoopbackWorld::open(const std::vector<std::uint8_t>& key,
                         const std::uint8_t* nonce, std::size_t nonce_size,
                         const std::uint8_t* aad, std::size_t aad_size,
                         const std::uint8_t* input, std::size_t input_size,
                         std::vector<std::uint8_t>* out) const
{
    if (out == nullptr || input == nullptr || input_size < 16) return false;
    const std::size_t plain_size = input_size - 16;
    std::vector<std::uint8_t> preimage;
    static constexpr char domain[] = "flynes-loopback-aead-v1";
    preimage.insert(preimage.end(), domain, domain + sizeof(domain) - 1);
    if (nonce != nullptr && nonce_size != 0)
        preimage.insert(preimage.end(), nonce, nonce + nonce_size);
    if (aad != nullptr && aad_size != 0)
        preimage.insert(preimage.end(), aad, aad + aad_size);
    preimage.insert(preimage.end(), input, input + plain_size);
    const auto tag = hmac(key, preimage.data(), preimage.size());
    if (std::memcmp(tag.data(), input + plain_size, 16) != 0) return false;
    out->assign(input, input + plain_size);
    return true;
}

std::array<std::uint8_t, 64> LoopbackWorld::sign(
    const LoopbackPoint& key, const std::uint8_t* domain, std::size_t domain_size,
    const std::uint8_t digest[32]) const
{
    /* Deterministic, and canonically encoded on purpose: the wire codecs require a
     * canonical low-S signature, so the mock produces one instead of being
     * exempted from the check. */
    std::array<std::uint8_t, 64> out{};
    for (std::size_t part = 0; part < 2; ++part)
    {
        std::vector<std::uint8_t> input;
        static constexpr char prefix[] = "flynes-loopback-sig-v1";
        input.insert(input.end(), prefix, prefix + sizeof(prefix) - 1);
        input.push_back(static_cast<std::uint8_t>(part + 1));
        if (domain != nullptr && domain_size != 0)
            input.insert(input.end(), domain, domain + domain_size);
        input.insert(input.end(), key.begin(), key.end());
        if (digest != nullptr)
            input.insert(input.end(), digest, digest + 32);
        const auto hash = wire::sha256(input.data(), input.size());
        std::copy(hash.begin(), hash.end(),
                  out.begin() + static_cast<std::ptrdiff_t>(part * 32));
    }
    out[0] = static_cast<std::uint8_t>((out[0] & 0x7fu) | 0x01u);
    out[32] = static_cast<std::uint8_t>(0x40u | (out[32] & 0x1fu));
    return out;
}

bool LoopbackWorld::verify(const LoopbackPoint& key, const std::uint8_t* domain,
                           std::size_t domain_size, const std::uint8_t digest[32],
                           const std::uint8_t signature[64]) const
{
    if (signature == nullptr || digest == nullptr) return false;
    const auto expected = sign(key, domain, domain_size, digest);
    return std::memcmp(expected.data(), signature, expected.size()) == 0;
}

std::vector<std::uint8_t> LoopbackWorld::random(LoopbackSide side,
                                                std::size_t size,
                                                const std::uint8_t* purpose,
                                                std::size_t purpose_size)
{
    /* Counter-derived so two calls never coincide, and salted with the side so the
     * two engines cannot accidentally share a nonce. Equal randomness on both
     * sides would hide real defects rather than expose them. */
    std::vector<std::uint8_t> out;
    out.reserve(size);
    std::uint32_t block_index = 0;
    while (out.size() < size)
    {
        std::vector<std::uint8_t> input;
        static constexpr char domain[] = "flynes-loopback-random-v1";
        input.insert(input.end(), domain, domain + sizeof(domain) - 1);
        input.push_back(static_cast<std::uint8_t>(side));
        const std::uint64_t counter = random_counter;
        for (int shift = 56; shift >= 0; shift -= 8)
            input.push_back(static_cast<std::uint8_t>(
                counter >> static_cast<unsigned>(shift)));
        input.push_back(static_cast<std::uint8_t>(block_index >> 24u));
        input.push_back(static_cast<std::uint8_t>(block_index >> 16u));
        input.push_back(static_cast<std::uint8_t>(block_index >> 8u));
        input.push_back(static_cast<std::uint8_t>(block_index));
        if (purpose != nullptr && purpose_size != 0)
            input.insert(input.end(), purpose, purpose + purpose_size);
        const auto digest = wire::sha256(input.data(), input.size());
        out.insert(out.end(), digest.begin(), digest.end());
        ++block_index;
    }
    ++random_counter;
    out.resize(size);
    return out;
}

void loopback_deliver_buffer(fly_session_inbox_v2_t* inbox,
                             const fly_session_op_token_v2& token,
                             std::uint32_t kind, const std::uint8_t* bytes,
                             std::size_t size)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{
        bytes, static_cast<std::uint32_t>(size), 0};
    if (fly_session_buffer_create_copy_v2(source, &buffer) != FLY_SESSION_V2_OK)
        return;
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = buffer;
    payload.logical_size = size;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    const auto delivered = fly_session_deliver_v2(inbox, &event);
    fly_session_buffer_release_v2(buffer);
    /*
     * A provider callback may NOT complete its own operation: the engine rejects a
     * terminal delivered from inside the callback (the completion record does not
     * exist yet), and the refusal is silent, so the operation would stay pending
     * forever. The pump hands these out after the callback returned; a refusal here
     * is a real defect and is reported instead of dropped.
     */
    check(delivered == FLY_SESSION_V2_ACCEPTED,
          "a loopback provider buffer terminal must be accepted by the engine, never "
          "silently dropped");
}

void loopback_deliver_resource(fly_session_inbox_v2_t* inbox,
                               const fly_session_op_token_v2& token,
                               std::uint32_t kind,
                               fly_session_resource_handle_v2 resource)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    payload.generation = token.connection_generation;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(inbox, &event) == FLY_SESSION_V2_ACCEPTED,
          "a loopback provider resource terminal must be accepted by the engine, "
          "never silently dropped");
}

void loopback_deliver_end(fly_session_inbox_v2_t* inbox,
                          const fly_session_op_token_v2& token,
                          std::uint32_t kind, fly_session_result_v2 result)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = result;
    event.payload_kind = kind;
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(inbox, &event) == FLY_SESSION_V2_ACCEPTED,
          "a loopback provider end terminal must be accepted by the engine, never "
          "silently dropped");
}

void loopback_deliver_verification(fly_session_inbox_v2_t* inbox,
                                   const fly_session_op_token_v2& token,
                                   fly_session_result_v2 result)
{
    loopback_deliver_end(inbox, token,
                         FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2, result);
}

} // namespace flynes::session::loopback
