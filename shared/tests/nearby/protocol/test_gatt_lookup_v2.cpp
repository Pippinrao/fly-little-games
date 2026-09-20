#include "gatt_lookup_v2.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using flynes::session::wire::GattLookupDirection;
using flynes::session::wire::GattLookupMessageV2;
using flynes::session::wire::GattLookupTypeV2;
using flynes::session::wire::Status;

namespace {

namespace fs = std::filesystem;

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::array<std::uint8_t, 80> pair_context()
{
    std::array<std::uint8_t, 80> out{};
    out[1] = 1;       // PairContext object encoding V1.
    out[9] = 2;       // pair_wire_major=2.
    out[11] = 0;      // pair_wire_minor=0.
    out[12] = 1;      // BLE_ANONYMOUS.
    for (std::size_t i = 0; i < 16; ++i)
    {
        out[16 + i] = static_cast<std::uint8_t>(0x10u + i);
        out[32 + i] = static_cast<std::uint8_t>(0x30u + i);
        out[64 + i] = static_cast<std::uint8_t>(0x70u + i);
    }
    out[50] = 0xea;   // valid_for_ms=60000.
    out[51] = 0x60;
    return out;
}

std::array<std::uint8_t, 16> request_nonce()
{
    std::array<std::uint8_t, 16> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = static_cast<std::uint8_t>(0xa0u + i);
    return out;
}

void test_request_exact_bytes()
{
    const std::array<std::uint8_t, 6> code{{'0', '1', '2', '3', '4', '5'}};
    const auto nonce = request_nonce();
    std::array<std::uint8_t, 72> encoded{};
    std::size_t written = 0;
    check(flynes::session::wire::encode_gatt_lookup_request_v2(
              code, nonce, encoded.data(), encoded.size(), &written) == Status::Ok,
          "request encodes");
    check(written == encoded.size(), "request is exactly 72 bytes");
    const std::array<std::uint8_t, 16> prefix{{
        2, 27, 0, 0, 0, 0, 0, 32,
        0, 2, 0, 0, 0, 0, 0, 0}};
    check(std::equal(prefix.begin(), prefix.end(), encoded.begin()),
          "request outer/header/body prefix");
    check(std::equal(code.begin(), code.end(), encoded.begin() + 16),
          "leading-zero code at body offset 8");
    check(std::equal(nonce.begin(), nonce.end(), encoded.begin() + 24),
          "request nonce at body offset 16");

    // Independent SHA-256 oracle over
    // "flynes-gatt-logical-v2" || u32be(40) || exact header+body.
    const std::array<std::uint8_t, 32> expected_hash{{
        0x2b, 0x81, 0xf0, 0xda, 0x09, 0x41, 0x3a, 0xde,
        0x20, 0xd4, 0xba, 0x34, 0xae, 0x9e, 0x1e, 0x43,
        0xda, 0x3d, 0x26, 0xf1, 0x4d, 0x5e, 0x8b, 0x79,
        0xe1, 0x7f, 0x01, 0xfd, 0x29, 0xe5, 0x33, 0xb6}};
    check(std::equal(expected_hash.begin(), expected_hash.end(), encoded.begin() + 40),
          "request literal logical hash");
    std::ifstream request_golden(fs::path(FLYNES_GATT_LOOKUP_V2_DIR) /
                                 "code_lookup_request.bin", std::ios::binary);
    const std::vector<std::uint8_t> request_bytes{
        std::istreambuf_iterator<char>(request_golden), std::istreambuf_iterator<char>()};
    check(request_bytes == std::vector<std::uint8_t>(encoded.begin(), encoded.end()),
          "request matches generated canonical golden");

    GattLookupMessageV2 decoded{};
    check(flynes::session::wire::decode_gatt_lookup_v2(
              GattLookupDirection::ResponderToInitiator,
              encoded.data(), encoded.size(), &decoded) == Status::Ok,
          "request decodes in the only legal direction");
    check(decoded.type == GattLookupTypeV2::CodeLookupRequest &&
              decoded.body_size == 32 && decoded.body == encoded.data() + 8,
          "request decode is a borrowed exact body");
    check(flynes::session::wire::decode_gatt_lookup_v2(
              GattLookupDirection::InitiatorToResponder,
              encoded.data(), encoded.size(), &decoded) != Status::Ok,
          "request reflected direction rejected");
}

void test_match_and_context_binding()
{
    const auto nonce = request_nonce();
    const auto context = pair_context();
    std::array<std::uint8_t, 176> encoded{};
    std::size_t written = 0;
    check(flynes::session::wire::encode_gatt_lookup_match_v2(
              nonce, context, encoded.data(), encoded.size(), &written) == Status::Ok,
          "match encodes");
    check(written == encoded.size() && encoded[0] == 2 && encoded[1] == 28 &&
              encoded[7] == 136 && encoded[9] == 2,
          "match exact outer/body versions and length");
    check(std::equal(nonce.begin(), nonce.end(), encoded.begin() + 16),
          "match echoes request nonce");
    check(std::equal(context.begin(), context.end(), encoded.begin() + 32),
          "match embeds exact PairContext bytes");
    std::ifstream match_golden(fs::path(FLYNES_GATT_LOOKUP_V2_DIR) /
                               "code_lookup_match.bin", std::ios::binary);
    const std::vector<std::uint8_t> match_bytes{
        std::istreambuf_iterator<char>(match_golden), std::istreambuf_iterator<char>()};
    check(match_bytes == std::vector<std::uint8_t>(encoded.begin(), encoded.end()),
          "match matches generated canonical golden");

    GattLookupMessageV2 decoded{};
    check(flynes::session::wire::decode_gatt_lookup_v2(
              GattLookupDirection::InitiatorToResponder,
              encoded.data(), encoded.size(), &decoded) == Status::Ok,
          "match decodes in the only legal direction");
    check(flynes::session::wire::lookup_match_binds_request_v2(decoded, nonce, context),
          "match binds exact request nonce and PairContext");
    auto wrong_nonce = nonce;
    ++wrong_nonce[0];
    check(!flynes::session::wire::lookup_match_binds_request_v2(decoded, wrong_nonce, context),
          "nonce substitution rejected");
    auto wrong_context = context;
    ++wrong_context[32];
    check(!flynes::session::wire::lookup_match_binds_request_v2(decoded, nonce, wrong_context),
          "PairContext substitution rejected");
}

void test_negative_registry_and_shape()
{
    const std::array<std::uint8_t, 6> code{{'0', '1', '2', '3', '4', '5'}};
    const auto nonce = request_nonce();
    std::array<std::uint8_t, 72> valid{};
    std::size_t written = 0;
    check(flynes::session::wire::encode_gatt_lookup_request_v2(
              code, nonce, valid.data(), valid.size(), &written) == Status::Ok,
          "negative fixture starts valid");

    GattLookupMessageV2 decoded{};
    for (const std::size_t size : {0u, 7u, 39u, 71u})
        check(flynes::session::wire::decode_gatt_lookup_v2(
                  GattLookupDirection::ResponderToInitiator,
                  valid.data(), size, &decoded) == Status::Truncated,
              "truncated request rejected");
    std::array<std::uint8_t, 73> trailing{};
    std::copy(valid.begin(), valid.end(), trailing.begin());
    check(flynes::session::wire::decode_gatt_lookup_v2(
              GattLookupDirection::ResponderToInitiator,
              trailing.data(), trailing.size(), &decoded) == Status::Trailing,
          "trailing request rejected");

    const std::array<std::pair<std::size_t, std::uint8_t>, 9> mutations{{
        {std::size_t{0}, std::uint8_t{1}},
        {std::size_t{0}, std::uint8_t{3}},
        {std::size_t{1}, std::uint8_t{28}},
        {std::size_t{1}, std::uint8_t{29}},
        {std::size_t{2}, std::uint8_t{1}},
        {std::size_t{8}, std::uint8_t{1}},
        {std::size_t{9}, std::uint8_t{1}},
        {std::size_t{14}, std::uint8_t{1}},
        {std::size_t{15}, std::uint8_t{1}}}};
    for (const auto& mutation : mutations)
    {
        auto bytes = valid;
        bytes[mutation.first] = mutation.second;
        check(flynes::session::wire::decode_gatt_lookup_v2(
                  GattLookupDirection::ResponderToInitiator,
                  bytes.data(), bytes.size(), &decoded) != Status::Ok,
              "version/type/reserved mutation rejected");
    }
    auto zero_nonce = valid;
    std::fill(zero_nonce.begin() + 24, zero_nonce.begin() + 40, std::uint8_t{0});
    check(flynes::session::wire::decode_gatt_lookup_v2(
              GattLookupDirection::ResponderToInitiator,
              zero_nonce.data(), zero_nonce.size(), &decoded) != Status::Ok,
          "zero request nonce rejected");
    auto bad_hash = valid;
    ++bad_hash.back();
    check(flynes::session::wire::decode_gatt_lookup_v2(
              GattLookupDirection::ResponderToInitiator,
              bad_hash.data(), bad_hash.size(), &decoded) != Status::Ok,
          "logical hash substitution rejected");

    check(!flynes::session::wire::gatt_lookup_type_allowed_v2(27, 1),
          "V1 type 27 rejected");
    check(!flynes::session::wire::gatt_lookup_type_allowed_v2(28, 1),
          "V1 type 28 rejected");
    check(!flynes::session::wire::gatt_lookup_type_allowed_v2(29, 1),
          "V1 type 29 rejected");
    check(!flynes::session::wire::gatt_lookup_type_allowed_v2(29, 2),
          "V2 type 29 rejected");
    check(flynes::session::wire::gatt_lookup_type_allowed_v2(27, 2) &&
              flynes::session::wire::gatt_lookup_type_allowed_v2(28, 2),
          "only V2 type 27/28 registered");
}

} // namespace

int main()
{
    check(fs::exists(fs::path(FLYNES_GATT_LOOKUP_V2_DIR) / "gatt_lookup_v2.schema"),
          "authoritative lookup schema exists");
    test_request_exact_bytes();
    test_match_and_context_binding();
    test_negative_registry_and_shape();
    if (failures != 0)
        return 1;
    std::cout << "GATT lookup V2 exact codec passed\n";
    return 0;
}
