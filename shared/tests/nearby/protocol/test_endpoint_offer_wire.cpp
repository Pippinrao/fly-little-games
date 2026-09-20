#include "endpoint_offer.hpp"

#include <array>
#include <cstdio>

namespace {
using namespace flynes::session::wire;

int failures = 0;
void check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

void exact_initial_offer_contract()
{
    std::array<std::uint8_t, 32> transcript{};
    std::array<std::uint8_t, 16> session{};
    std::array<std::uint8_t, 32> credential_hash{};
    std::array<std::uint8_t, 32> spki{};
    std::array<std::uint8_t, 16> endpoint{};
    transcript[0] = 1; session[0] = 2; credential_hash[0] = 3;
    spki[0] = 4; endpoint[12] = 192; endpoint[13] = 168;
    endpoint[14] = 1; endpoint[15] = 7;
    const auto binding = initial_bearer_binding_hash_v1(
        transcript, PairRoleV1::Initiator, credential_hash);
    std::array<std::uint8_t, kEndpointOfferPlaintextSizeV1> plaintext{};
    check(encode_initial_endpoint_offer_plaintext_v1(
              transcript, session, PairRoleV1::Responder,
              EndpointKindV1::Ipv4, endpoint, 55000, binding, spki,
              &plaintext) == Status::Ok && plaintext[88] == 1 &&
              plaintext[89] == 2 && plaintext[90] == 1 &&
              plaintext[112] == 0xd6 && plaintext[113] == 0xd8,
          "initial endpoint plaintext is exact 184 bytes");
    EndpointOfferPlaintextV1 decoded{};
    check(decode_initial_endpoint_offer_plaintext_v1(
              plaintext.data(), plaintext.size(), transcript, session,
              PairRoleV1::Responder, EndpointKindV1::Ipv4, binding, spki,
              &decoded) == Status::Ok && decoded.endpoint_value == endpoint &&
              decoded.port == 55000,
          "initial endpoint plaintext validates every immutable binding");

    std::array<std::uint8_t, 12> nonce{};
    nonce[0] = 5;
    std::array<std::uint8_t, kEndpointOfferAadSizeV1> aad{};
    check(build_endpoint_offer_aad_v1(
              PairRoleV1::Responder, EndpointBindingKindV1::Initial,
              transcript, session, {}, &aad) == Status::Ok && aad[20] == 22 &&
              aad[21] == 2 && aad[22] == 1 && aad[23] == 1 && aad[107] == 184,
          "endpoint offer AAD is exact 108 bytes");
    std::array<std::uint8_t, 200> ciphertext{};
    ciphertext[0] = 6;
    std::array<std::uint8_t, kEndpointOfferEnvelopeSizeV1> envelope{};
    check(encode_endpoint_offer_envelope_v1(
              nonce, ciphertext.data(), ciphertext.size(), &envelope) ==
              Status::Ok && envelope[0] == 1 && envelope[4] == 5 &&
              envelope[16] == 6,
          "endpoint offer envelope is exact 216 bytes");
    EndpointOfferEnvelopeV1 opened{};
    check(decode_endpoint_offer_envelope_v1(
              envelope.data(), envelope.size(), &opened) == Status::Ok &&
              opened.public_nonce == nonce && opened.ciphertext_and_tag.size() == 200,
          "endpoint offer envelope round trips nonce and ciphertext");
}

void negative_endpoint_matrix()
{
    std::array<std::uint8_t, 32> transcript{};
    std::array<std::uint8_t, 16> session{};
    std::array<std::uint8_t, 32> binding{};
    std::array<std::uint8_t, 32> spki{};
    std::array<std::uint8_t, 16> endpoint{};
    transcript[0] = session[0] = binding[0] = spki[0] = 1;
    endpoint[0] = 1;
    std::array<std::uint8_t, kEndpointOfferPlaintextSizeV1> plaintext{};
    check(encode_initial_endpoint_offer_plaintext_v1(
              transcript, session, PairRoleV1::Initiator,
              EndpointKindV1::Ipv4, endpoint, 55000, binding, spki,
              &plaintext) == Status::InvalidField,
          "IPv4 rejects nonzero leading twelve bytes");
    endpoint.fill(0); endpoint[15] = 1;
    check(encode_initial_endpoint_offer_plaintext_v1(
              transcript, session, PairRoleV1::Initiator,
              EndpointKindV1::Ipv4, endpoint, 49151, binding, spki,
              &plaintext) == Status::InvalidField,
          "numeric endpoint rejects non-dynamic port");
    endpoint.fill(0); endpoint[0] = 1;
    check(encode_initial_endpoint_offer_plaintext_v1(
              transcript, session, PairRoleV1::Initiator,
              EndpointKindV1::AppleBonjourP2p, endpoint, 1, binding, spki,
              &plaintext) == Status::InvalidField,
          "Bonjour endpoint requires zero port");
}

} // namespace

int main()
{
    exact_initial_offer_contract();
    negative_endpoint_matrix();
    if (failures != 0) return 1;
    std::puts("endpoint offer wire tests passed");
    return 0;
}
