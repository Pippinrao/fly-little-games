#ifndef FLYNES_SESSION_WIRE_PAIR_CRYPTO_HPP
#define FLYNES_SESSION_WIRE_PAIR_CRYPTO_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::session::wire {

enum class PairCryptoStatus : std::int32_t
{
    Ok = 0,
    Retry = 1,
    InvalidArgument = -1,
    OutputTooLarge = -2
};

std::array<std::uint8_t, 32> hmac_sha256(
    const std::uint8_t* key, std::size_t key_size,
    const std::uint8_t* data, std::size_t data_size);

std::array<std::uint8_t, 32> hkdf_extract_sha256(
    const std::uint8_t* salt, std::size_t salt_size,
    const std::uint8_t* ikm, std::size_t ikm_size);

PairCryptoStatus hkdf_expand_sha256(
    const std::uint8_t* prk, std::size_t prk_size,
    const std::uint8_t* info, std::size_t info_size,
    std::uint8_t* out, std::size_t out_size);

std::array<std::uint8_t, 32> pair_commitment_v1(
    const std::uint8_t* contribution, std::size_t contribution_size);

PairCryptoStatus pair_sas_from_blocks(
    const std::array<std::uint8_t, 32>* blocks,
    std::size_t block_count,
    std::array<std::uint8_t, 6>* out) noexcept;

PairCryptoStatus derive_pair_sas_v1(
    const std::array<std::uint8_t, 32>& sas_key,
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    std::uint32_t maximum_retry_blocks,
    std::array<std::uint8_t, 6>* out);

} // namespace flynes::session::wire

#endif
