#include "pair_crypto.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace flynes::session::wire {

std::array<std::uint8_t, 32> hmac_sha256(
    const std::uint8_t* key, std::size_t key_size,
    const std::uint8_t* data, std::size_t data_size)
{
    std::array<std::uint8_t, 64> key_block{};
    if (key_size > key_block.size())
    {
        const auto key_hash = sha256(key, key_size);
        std::copy(key_hash.begin(), key_hash.end(), key_block.begin());
    }
    else if (key_size != 0)
    {
        std::copy(key, key + key_size, key_block.begin());
    }
    std::array<std::uint8_t, 64> inner_pad{};
    std::array<std::uint8_t, 64> outer_pad{};
    for (std::size_t i = 0; i < key_block.size(); ++i)
    {
        inner_pad[i] = static_cast<std::uint8_t>(key_block[i] ^ 0x36u);
        outer_pad[i] = static_cast<std::uint8_t>(key_block[i] ^ 0x5cu);
    }
    std::vector<std::uint8_t> inner(inner_pad.begin(), inner_pad.end());
    if (data_size != 0)
        inner.insert(inner.end(), data, data + data_size);
    const auto inner_hash = sha256(inner.data(), inner.size());
    std::array<std::uint8_t, 96> outer{};
    std::copy(outer_pad.begin(), outer_pad.end(), outer.begin());
    std::copy(inner_hash.begin(), inner_hash.end(), outer.begin() + 64);
    return sha256(outer.data(), outer.size());
}

std::array<std::uint8_t, 32> hkdf_extract_sha256(
    const std::uint8_t* salt, std::size_t salt_size,
    const std::uint8_t* ikm, std::size_t ikm_size)
{
    const std::array<std::uint8_t, 32> zero_salt{};
    if (salt_size == 0)
        return hmac_sha256(zero_salt.data(), zero_salt.size(), ikm, ikm_size);
    return hmac_sha256(salt, salt_size, ikm, ikm_size);
}

PairCryptoStatus hkdf_expand_sha256(
    const std::uint8_t* prk, std::size_t prk_size,
    const std::uint8_t* info, std::size_t info_size,
    std::uint8_t* out, std::size_t out_size)
{
    if ((prk == nullptr && prk_size != 0) || (info == nullptr && info_size != 0) ||
        (out == nullptr && out_size != 0))
        return PairCryptoStatus::InvalidArgument;
    if (out_size > 255u * 32u)
        return PairCryptoStatus::OutputTooLarge;
    std::array<std::uint8_t, 32> prior{};
    std::size_t prior_size = 0;
    std::size_t written = 0;
    std::uint8_t counter = 1;
    while (written < out_size)
    {
        std::vector<std::uint8_t> input;
        input.reserve(prior_size + info_size + 1);
        input.insert(input.end(), prior.begin(),
                     prior.begin() + static_cast<std::ptrdiff_t>(prior_size));
        if (info_size != 0)
            input.insert(input.end(), info, info + info_size);
        input.push_back(counter);
        prior = hmac_sha256(prk, prk_size, input.data(), input.size());
        prior_size = prior.size();
        const std::size_t count = std::min(prior.size(), out_size - written);
        std::copy(prior.begin(), prior.begin() + static_cast<std::ptrdiff_t>(count), out + written);
        written += count;
        ++counter;
    }
    return PairCryptoStatus::Ok;
}

std::array<std::uint8_t, 32> pair_commitment_v1(
    const std::uint8_t* contribution, std::size_t contribution_size)
{
    return domain_hash("flynes-pair-commit-v1", contribution, contribution_size);
}

PairCryptoStatus pair_sas_from_blocks(
    const std::array<std::uint8_t, 32>* blocks, std::size_t block_count,
    std::array<std::uint8_t, 6>* out) noexcept
{
    if (blocks == nullptr || block_count == 0 || out == nullptr)
        return PairCryptoStatus::InvalidArgument;
    for (std::size_t block_index = 0; block_index < block_count; ++block_index)
    {
        for (std::size_t offset = 0; offset < 32; offset += 4)
        {
            const auto& block = blocks[block_index];
            const std::uint32_t candidate =
                (static_cast<std::uint32_t>(block[offset]) << 24u) |
                (static_cast<std::uint32_t>(block[offset + 1]) << 16u) |
                (static_cast<std::uint32_t>(block[offset + 2]) << 8u) |
                block[offset + 3];
            if (candidate >= UINT32_C(4294000000))
                continue;
            std::uint32_t value = candidate % UINT32_C(1000000);
            for (std::size_t i = 0; i < out->size(); ++i)
            {
                (*out)[out->size() - i - 1] =
                    static_cast<std::uint8_t>('0' + value % 10);
                value /= 10;
            }
            return PairCryptoStatus::Ok;
        }
    }
    return PairCryptoStatus::Retry;
}

PairCryptoStatus derive_pair_sas_v1(
    const std::array<std::uint8_t, 32>& sas_key,
    const std::array<std::uint8_t, 32>& transcript,
    std::uint32_t maximum_retry_blocks,
    std::array<std::uint8_t, 6>* out)
{
    if (out == nullptr)
        return PairCryptoStatus::InvalidArgument;
    auto block = hmac_sha256(sas_key.data(), sas_key.size(),
                             transcript.data(), transcript.size());
    if (pair_sas_from_blocks(&block, 1, out) == PairCryptoStatus::Ok)
        return PairCryptoStatus::Ok;
    static constexpr char kRetryDomain[] = "flynes-sas-retry-v1";
    for (std::uint32_t retry = 1; retry <= maximum_retry_blocks; ++retry)
    {
        std::array<std::uint8_t, sizeof(kRetryDomain) - 1 + 32 + 4> input{};
        std::memcpy(input.data(), kRetryDomain, sizeof(kRetryDomain) - 1);
        std::copy(transcript.begin(), transcript.end(),
                  input.begin() + static_cast<std::ptrdiff_t>(sizeof(kRetryDomain) - 1));
        const std::size_t offset = sizeof(kRetryDomain) - 1 + 32;
        input[offset] = static_cast<std::uint8_t>(retry >> 24u);
        input[offset + 1] = static_cast<std::uint8_t>(retry >> 16u);
        input[offset + 2] = static_cast<std::uint8_t>(retry >> 8u);
        input[offset + 3] = static_cast<std::uint8_t>(retry);
        block = hmac_sha256(sas_key.data(), sas_key.size(), input.data(), input.size());
        if (pair_sas_from_blocks(&block, 1, out) == PairCryptoStatus::Ok)
            return PairCryptoStatus::Ok;
        if (retry == std::numeric_limits<std::uint32_t>::max())
            break;
    }
    return PairCryptoStatus::Retry;
}

} // namespace flynes::session::wire
