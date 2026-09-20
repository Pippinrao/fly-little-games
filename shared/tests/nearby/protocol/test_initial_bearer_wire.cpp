#include "gatt_fragment.hpp"
#include "initial_bearer.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

namespace {
using namespace flynes::session::wire;

int failures = 0;
void check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

BearerPlanBytes plan()
{
    BearerPlanBytes value{};
    value[0] = 4;
    value[1] = 1;
    value[2] = 2;
    value[3] = 1;
    value[4] = 1;
    value[5] = 1;
    value[6] = 30;
    value[11] = 1;
    value[12] = 1;
    return value;
}

std::array<std::uint8_t, 100> network_credential()
{
    std::array<std::uint8_t, 100> value{};
    value[0] = 1;
    value[1] = 1;
    value[2] = 4;
    value[3] = 8;
    std::copy_n(reinterpret_cast<const std::uint8_t*>("TEST"), 4,
                value.begin() + 4);
    std::copy_n(reinterpret_cast<const std::uint8_t*>("password"), 8,
                value.begin() + 36);
    return value;
}

void exact_initial_credential_contract()
{
    const auto selected = plan();
    const auto credential = network_credential();
    std::array<std::uint8_t, kBearerJoinParamsSizeV1> join{};
    check(encode_bearer_join_params_v1(selected, 60000, credential, &join) ==
              Status::Ok && join[8] == 4 && join[11] == 1 &&
              join[16] == 0 && join[17] == 0 && join[18] == 0xea &&
              join[19] == 0x60 && join[21] == 100,
          "join params freeze exact roles, codec, lifetime, and length");
    BearerJoinParamsV1 decoded_join{};
    check(decode_bearer_join_params_v1(
              join.data(), join.size(), selected, &decoded_join) == Status::Ok &&
              decoded_join.valid_for_ms == 60000 &&
              decoded_join.credential == credential,
          "join params round trip canonical credential bytes");

    std::array<std::uint8_t, 32> transcript{};
    std::array<std::uint8_t, 16> session{};
    transcript[0] = 1;
    session[0] = 2;
    std::array<std::uint8_t, kInitialBearerCredentialPlaintextSizeV1> plaintext{};
    check(encode_initial_bearer_credential_plaintext_v1(
              transcript, session, PairRoleV1::Initiator, selected, join,
              &plaintext) == Status::Ok && plaintext[56] == 1 &&
              plaintext[57] == 2 && plaintext[64 + 21] == 100,
          "type8 plaintext freezes exact 600-byte creator binding");
    InitialBearerCredentialPlaintextV1 decoded{};
    check(decode_initial_bearer_credential_plaintext_v1(
              plaintext.data(), plaintext.size(), transcript, session,
              PairRoleV1::Initiator, selected, &decoded) == Status::Ok &&
              decoded.join.credential == credential,
          "type8 plaintext validates transcript, session, roles and plan");

    std::array<std::uint8_t, kInitialBearerCredentialNonceSizeV1> nonce{};
    std::array<std::uint8_t, kInitialBearerCredentialAadSizeV1> aad{};
    check(build_initial_bearer_credential_nonce_v1(
              PairRoleV1::Initiator, 1, &nonce) == Status::Ok &&
              nonce[0] == 0x49 && nonce[1] == 0x32 && nonce[2] == 0x52 &&
              nonce[3] == 0x01 && nonce[11] == 1,
          "type8 initiator nonce freezes direction and counter");
    check(build_initial_bearer_credential_aad_v1(
              PairRoleV1::Initiator, transcript, session, 1, &aad) ==
              Status::Ok && aad[17] == 2 && aad[20] == 8 && aad[21] == 1 &&
              aad[22] == 2 && aad[79] == 1 && aad[82] == 2 && aad[83] == 88,
          "type8 AAD freezes service, wire, roles, IDs, counter, and length");

    std::array<std::uint8_t, 616> ciphertext{};
    ciphertext[0] = 3;
    std::array<std::uint8_t, kInitialBearerCredentialEnvelopeSizeV1> envelope{};
    check(encode_initial_bearer_credential_envelope_v1(
              1, ciphertext.data(), ciphertext.size(), &envelope) == Status::Ok &&
              envelope[0] == 1 && envelope[11] == 1 && envelope[12] == 3,
          "type8 envelope is exact 628 bytes");
    InitialBearerCredentialEnvelopeV1 decoded_envelope{};
    check(decode_initial_bearer_credential_envelope_v1(
              envelope.data(), envelope.size(), &decoded_envelope) == Status::Ok &&
              decoded_envelope.message_counter == 1 &&
              decoded_envelope.ciphertext_and_tag.size() == 616,
          "type8 envelope round trips exact ciphertext and tag");
    std::vector<std::uint8_t> logical;
    check(encode_gatt_logical_message(8, envelope.data(), envelope.size(),
                                      &logical) == GattFragmentResult::Accepted &&
              logical.size() == 668,
          "complete type8 GATT logical message is exact 668 bytes");
}

void credential_negative_matrix()
{
    auto credential = network_credential();
    credential[4 + credential[2]] = 1;
    check(validate_bearer_credential_v1(1, credential.data(), credential.size()) ==
              Status::InvalidField,
          "credential rejects nonzero name padding");
    credential = network_credential();
    credential[36] = 0;
    check(validate_bearer_credential_v1(1, credential.data(), credential.size()) ==
              Status::InvalidField,
          "credential rejects NUL/non-printable passphrase bytes");
    credential = network_credential();
    std::array<std::uint8_t, kBearerJoinParamsSizeV1> join{};
    check(encode_bearer_join_params_v1(plan(), 0, credential, &join) ==
              Status::InvalidField,
          "credential rejects expired zero lifetime");
    check(encode_bearer_join_params_v1(plan(), 60001, credential, &join) ==
              Status::InvalidField,
          "credential rejects lifetime beyond initial deadline");
    std::array<std::uint8_t, kInitialBearerCredentialEnvelopeSizeV1> envelope{};
    std::array<std::uint8_t, 616> ciphertext{};
    check(encode_initial_bearer_credential_envelope_v1(
              0, ciphertext.data(), ciphertext.size(), &envelope) ==
              Status::InvalidField,
          "credential envelope rejects counter zero");
}

} // namespace

int main()
{
    exact_initial_credential_contract();
    credential_negative_matrix();
    if (failures != 0) return 1;
    std::puts("initial bearer wire tests passed");
    return 0;
}
