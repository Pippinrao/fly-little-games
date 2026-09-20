#include "pair_crypto.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using flynes::session::wire::PairCryptoStatus;

namespace {

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<std::uint8_t> hex(const char* text)
{
    std::vector<std::uint8_t> out;
    const std::string value(text);
    for (std::size_t i = 0; i < value.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoul(value.substr(i, 2), nullptr, 16)));
    return out;
}

void test_rfc5869_case_1()
{
    const std::vector<std::uint8_t> ikm(22, 0x0b);
    const auto salt = hex("000102030405060708090a0b0c");
    const auto info = hex("f0f1f2f3f4f5f6f7f8f9");
    const auto expected_prk = hex(
        "077709362c2e32df0ddc3f0dc47bba63"
        "90b6c73bb50f9c3122ec844ad7c2b3e5");
    const auto expected_okm = hex(
        "3cb25f25faacd57a90434f64d0362f2a"
        "2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
        "34007208d5b887185865");
    const auto prk = flynes::session::wire::hkdf_extract_sha256(
        salt.data(), salt.size(), ikm.data(), ikm.size());
    check(std::equal(prk.begin(), prk.end(), expected_prk.begin()),
          "RFC 5869 case 1 PRK");
    std::vector<std::uint8_t> okm(42);
    check(flynes::session::wire::hkdf_expand_sha256(
              prk.data(), prk.size(), info.data(), info.size(),
              okm.data(), okm.size()) == PairCryptoStatus::Ok && okm == expected_okm,
          "RFC 5869 case 1 OKM");
}

void test_sas_rejection_sampling()
{
    std::array<std::uint8_t, 32> block{};
    block[0] = 0xff; block[1] = 0xf1; block[2] = 0x3d; block[3] = 0x80; // 4,294,000,000 reject.
    block[4] = 0xff; block[5] = 0xf1; block[6] = 0x3d; block[7] = 0x7f; // 4,293,999,999 accept.
    std::array<std::uint8_t, 6> sas{};
    check(flynes::session::wire::pair_sas_from_blocks(&block, 1, &sas) ==
              PairCryptoStatus::Ok && sas == std::array<std::uint8_t, 6>{{'9','9','9','9','9','9'}},
          "SAS rejects 4,294,000,000 then accepts 4,293,999,999");

    block.fill(0xff);
    check(flynes::session::wire::pair_sas_from_blocks(&block, 1, &sas) ==
              PairCryptoStatus::Retry,
          "whole rejected block requests the next HMAC block");
    block.fill(0);
    block[2] = 0x30; block[3] = 0x39;
    check(flynes::session::wire::pair_sas_from_blocks(&block, 1, &sas) ==
              PairCryptoStatus::Ok && sas == std::array<std::uint8_t, 6>{{'0','1','2','3','4','5'}},
          "SAS formatting is locale-independent with leading zeros");
}

void test_pair_domains()
{
    std::array<std::uint8_t, 320> contribution{};
    for (std::size_t i = 0; i < contribution.size(); ++i)
        contribution[i] = static_cast<std::uint8_t>(i & 0xffu);
    const auto commitment = flynes::session::wire::pair_commitment_v1(
        contribution.data(), contribution.size());
    const auto expected_commitment = hex(
        "cac165c3865d7cb7192fd0d71d5d65ad"
        "f996d5f72be7afc323252d0eddb442d7");
    check(std::equal(commitment.begin(), commitment.end(), expected_commitment.begin()),
          "pair commitment exact domain and u32be length");

    std::array<std::uint8_t, 32> sas_key{};
    std::array<std::uint8_t, 32> transcript{};
    for (std::size_t i = 0; i < 32; ++i)
    {
        sas_key[i] = static_cast<std::uint8_t>(i);
        transcript[i] = static_cast<std::uint8_t>(0x80u + i);
    }
    std::array<std::uint8_t, 6> sas{};
    check(flynes::session::wire::derive_pair_sas_v1(
              sas_key, transcript, 16, &sas) == PairCryptoStatus::Ok &&
              sas == std::array<std::uint8_t, 6>{{'3','9','6','2','4','8'}},
          "derived SAS independent literal");
}

} // namespace

int main()
{
    test_rfc5869_case_1();
    test_sas_rejection_sampling();
    test_pair_domains();
    if (failures != 0)
        return 1;
    std::cout << "pair HMAC/HKDF/SAS contract passed\n";
    return 0;
}
