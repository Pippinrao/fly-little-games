#include "session_signing_binding.hpp"
#include "p256_point.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace {

int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

std::array<std::uint8_t, 65> point(const char* hex)
{
    std::array<std::uint8_t, 65> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        const auto nibble = [](char c) {
            return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
        };
        out[i] = static_cast<std::uint8_t>((nibble(hex[i * 2]) << 4) |
                                            nibble(hex[i * 2 + 1]));
    }
    return out;
}

void exact_binding_round_trip()
{
    using namespace flynes::session::wire;
    const auto identity = point(
        "046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
        "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5");
    const auto session = point(
        "047cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978"
        "07775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1");
    std::array<std::uint8_t, 32> transcript{}; transcript.fill(0x11);
    std::array<std::uint8_t, 16> session_id{}; session_id.fill(0x22);
    std::array<std::uint8_t, kSessionSigningBindingPretagSizeV1> pretag{};
    std::array<std::uint8_t, 32> digest{};
    check(build_session_signing_binding_pretag_v1(
              transcript, session_id, PairRoleV1::Initiator, identity, session,
              validate_p256_uncompressed_point_callback, nullptr,
              &pretag, &digest) == Status::Ok &&
              digest == domain_hash("flynes-session-signing-key-binding-v1",
                                    pretag.data(), pretag.size()),
          "exact pretag produces the frozen prehashed-sign digest");
    std::array<std::uint8_t, 64> signature{};
    std::fill(signature.begin(), signature.begin() + 32,
              std::uint8_t{0x77});
    std::fill(signature.begin() + 32, signature.end(),
              std::uint8_t{0x44});
    std::array<std::uint8_t, kSessionSigningBindingSizeV1> binding{};
    std::array<std::uint8_t, 32> hash{};
    check(finish_session_signing_binding_v1(
              pretag, signature, &binding, &hash) == Status::Ok &&
              hash == domain_hash("flynes-session-signing-key-binding-hash-v1",
                                  binding.data(), binding.size()),
          "canonical provider signature finishes exact 312-byte binding");
    SessionSigningBindingV1 decoded{};
    check(decode_session_signing_binding_v1(
              binding.data(), binding.size(), transcript, session_id,
              PairRoleV1::Initiator, identity,
              validate_p256_uncompressed_point_callback, nullptr,
              &decoded) == Status::Ok &&
              decoded.session_signing_public_key == session &&
              decoded.signature == signature && decoded.digest == digest &&
              decoded.hash == hash,
          "binding round trip preserves exact authenticated fields");
    check(decode_session_signing_binding_v1(
              binding.data(), binding.size(), transcript, session_id,
              PairRoleV1::Responder, identity,
              validate_p256_uncompressed_point_callback, nullptr,
              &decoded) == Status::InvalidField,
          "reflected pair role is rejected");
    check(build_session_signing_binding_pretag_v1(
              transcript, session_id, PairRoleV1::Initiator, identity, identity,
              validate_p256_uncompressed_point_callback, nullptr,
              &pretag, &digest) == Status::InvalidField,
          "session signing key cannot reuse long-term identity key");
    signature.fill(0xff);
    check(finish_session_signing_binding_v1(
              pretag, signature, &binding, &hash) == Status::InvalidField,
          "out-of-range or high-S signature is rejected");
}

} // namespace

int main()
{
    exact_binding_round_trip();
    return failures == 0 ? 0 : 1;
}
