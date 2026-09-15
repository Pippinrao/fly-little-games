/*
 * W3 real loopback QUIC fixture implementation.
 *
 * The DER identity below is generated once by
 * out/tools/gen_loopback_identity.sh (openssl 3.0 in WSL) and spliced in
 * between the two markers. It is a throwaway test key: never trusted outside
 * this fixture, never a product secret.
 *
 * The runtime signature uses OpenSSL's libcrypto rather than a hand-rolled
 * P-256 implementation, because the provider verifies CertificateVerify with
 * ring and a hand-rolled version has to reproduce exactly what ring accepts.
 */

#include "loopback_quic_fixture.hpp"

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>

#include <cstring>
#include <memory>

namespace flynes::tests::nearby {
namespace {

/* >>> GENERATED LOOPBACK IDENTITY >>> */
/* --- BEGIN GENERATED LOOPBACK IDENTITY (out/tools/gen_loopback_identity.sh) --- */
const std::uint8_t kLoopbackPrivateKeyDerV1[] = {
48,129,135,2,1,0,48,19,6,7,42,134,72,206,61,2,
1,6,8,42,134,72,206,61,3,1,7,4,109,48,107,2,
1,1,4,32,180,50,82,93,35,61,115,28,192,120,58,195,
59,211,95,170,115,40,176,208,78,96,176,26,199,177,47,53,
56,241,25,90,161,68,3,66,0,4,123,215,192,108,239,129,
37,205,221,17,232,106,129,156,9,208,244,28,158,234,161,180,
112,140,37,219,37,67,89,138,231,234,130,116,227,64,199,249,
77,186,18,75,91,90,129,77,153,43,115,2,195,206,96,72,
246,45,6,34,217,102,155,96,135,18,
};
const std::size_t kLoopbackPrivateKeyDerV1_size = sizeof(kLoopbackPrivateKeyDerV1);
const std::uint8_t kLoopbackCertificateDerV1[] = {
48,130,1,148,48,130,1,57,160,3,2,1,2,2,20,43,
223,191,169,104,159,235,55,42,221,50,180,226,43,154,157,142,
88,118,126,48,10,6,8,42,134,72,206,61,4,3,2,48,
31,49,29,48,27,6,3,85,4,3,12,20,102,108,121,110,
101,115,45,108,111,111,112,98,97,99,107,45,116,101,115,116,
48,30,23,13,50,54,48,57,49,53,49,53,52,54,48,53,
90,23,13,52,54,48,57,49,48,49,53,52,54,48,53,90,
48,31,49,29,48,27,6,3,85,4,3,12,20,102,108,121,
110,101,115,45,108,111,111,112,98,97,99,107,45,116,101,115,
116,48,89,48,19,6,7,42,134,72,206,61,2,1,6,8,
42,134,72,206,61,3,1,7,3,66,0,4,123,215,192,108,
239,129,37,205,221,17,232,106,129,156,9,208,244,28,158,234,
161,180,112,140,37,219,37,67,89,138,231,234,130,116,227,64,
199,249,77,186,18,75,91,90,129,77,153,43,115,2,195,206,
96,72,246,45,6,34,217,102,155,96,135,18,163,83,48,81,
48,29,6,3,85,29,14,4,22,4,20,179,248,75,2,150,
52,48,106,196,130,101,196,8,39,123,27,95,67,231,112,48,
31,6,3,85,29,35,4,24,48,22,128,20,179,248,75,2,
150,52,48,106,196,130,101,196,8,39,123,27,95,67,231,112,
48,15,6,3,85,29,19,1,1,255,4,5,48,3,1,1,
255,48,10,6,8,42,134,72,206,61,4,3,2,3,73,0,
48,70,2,33,0,247,199,158,26,250,207,108,69,154,132,131,
114,174,223,181,3,74,143,86,64,93,10,181,22,102,195,69,
230,91,60,15,247,2,33,0,132,163,76,28,42,137,141,187,
40,95,199,227,97,239,22,36,22,209,186,11,195,21,59,23,
95,113,59,150,93,132,164,217,
};
const std::size_t kLoopbackCertificateDerV1_size = sizeof(kLoopbackCertificateDerV1);
const std::uint8_t kLoopbackSpkiDerV1[] = {
48,89,48,19,6,7,42,134,72,206,61,2,1,6,8,42,
134,72,206,61,3,1,7,3,66,0,4,123,215,192,108,239,
129,37,205,221,17,232,106,129,156,9,208,244,28,158,234,161,
180,112,140,37,219,37,67,89,138,231,234,130,116,227,64,199,
249,77,186,18,75,91,90,129,77,153,43,115,2,195,206,96,
72,246,45,6,34,217,102,155,96,135,18,
};
const std::size_t kLoopbackSpkiDerV1_size = sizeof(kLoopbackSpkiDerV1);
/* --- END GENERATED LOOPBACK IDENTITY --- */
/* <<< GENERATED LOOPBACK IDENTITY <<< */

using EcKeyPtr = std::unique_ptr<EC_KEY, decltype(&EC_KEY_free)>;
using BnPtr = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
using SigPtr = std::unique_ptr<ECDSA_SIG, decltype(&ECDSA_SIG_free)>;

/*
 * Largest DER ECDSA signature for P-256: SEQUENCE(2) + two INTEGERs of at most
 * 33 bytes. The provider's own callback buffer is 80 bytes.
 */
constexpr std::size_t kMaxDerSignatureSize = 80;

/*
 * Load the embedded test private key once. The provider calls the signer from
 * its own worker threads, so the key is treated as read-only after load; EC_KEY
 * signing is safe for concurrent readers, and the probe only ever runs one
 * handshake at a time.
 */
EC_KEY* test_signing_key() noexcept
{
    static EC_KEY* key = []() -> EC_KEY* {
        const LoopbackTlsIdentityV1& identity = loopback_tls_identity_v1();
        const unsigned char* cursor = identity.private_key_der.data();
        EVP_PKEY* pkey = d2i_AutoPrivateKey(
            nullptr, &cursor,
            static_cast<long>(identity.private_key_der.size()));
        if (pkey == nullptr)
            return nullptr;
        EC_KEY* ec = EVP_PKEY_get1_EC_KEY(pkey);
        EVP_PKEY_free(pkey);
        if (ec == nullptr)
            return nullptr;
        if (EC_KEY_check_key(ec) != 1)
        {
            EC_KEY_free(ec);
            return nullptr;
        }
        return ec;
    }();
    return key;
}

/*
 * Force s into the low half of the curve order so the provider accepts it
 * (BIP-62 style). Raw ECDSA output is high-S roughly half the time.
 */
bool normalize_low_s(ECDSA_SIG* signature) noexcept
{
    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
    ECDSA_SIG_get0(signature, &r, &s);
    if (r == nullptr || s == nullptr)
        return false;

    BnPtr order(BN_new(), BN_free);
    BnPtr half(BN_new(), BN_free);
    BnPtr corrected(BN_new(), BN_free);
    if (order == nullptr || half == nullptr || corrected == nullptr)
        return false;

    const EC_GROUP* group = EC_KEY_get0_group(test_signing_key());
    if (group == nullptr)
        return false;
    if (EC_GROUP_get_order(group, order.get(), nullptr) != 1)
        return false;
    if (BN_rshift1(half.get(), order.get()) != 1)
        return false;

    if (BN_cmp(s, half.get()) <= 0)
        return true; /* already low-S */

    if (BN_sub(corrected.get(), order.get(), s) != 1)
        return false;
    BnPtr new_s(BN_dup(corrected.get()), BN_free);
    if (new_s == nullptr)
        return false;
    /* ECDSA_SIG_set0 takes ownership of both BIGNUMs. */
    BnPtr new_r(BN_dup(r), BN_free);
    if (new_r == nullptr)
        return false;
    if (ECDSA_SIG_set0(signature, new_r.release(), new_s.release()) != 1)
        return false;
    return true;
}

} // namespace

const LoopbackTlsIdentityV1& loopback_tls_identity_v1()
{
    static const LoopbackTlsIdentityV1 identity = []() {
        LoopbackTlsIdentityV1 value;
        value.private_key_der.assign(
            kLoopbackPrivateKeyDerV1,
            kLoopbackPrivateKeyDerV1 + kLoopbackPrivateKeyDerV1_size);
        value.certificate_der.assign(
            kLoopbackCertificateDerV1,
            kLoopbackCertificateDerV1 + kLoopbackCertificateDerV1_size);
        value.spki_der.assign(kLoopbackSpkiDerV1,
                              kLoopbackSpkiDerV1 + kLoopbackSpkiDerV1_size);
        return value;
    }();
    return identity;
}

std::vector<std::uint8_t> loopback_spki_pin_v1()
{
    const LoopbackTlsIdentityV1& identity = loopback_tls_identity_v1();
    std::vector<std::uint8_t> pin(SHA256_DIGEST_LENGTH);
    unsigned char digest[SHA256_DIGEST_LENGTH] = {};
    SHA256(identity.spki_der.data(), identity.spki_der.size(), digest);
    std::memcpy(pin.data(), digest, SHA256_DIGEST_LENGTH);
    return pin;
}

std::size_t loopback_tls_sign_sha256_v1(const std::uint8_t* message,
                                        std::size_t message_size,
                                        std::uint8_t* out,
                                        std::size_t capacity) noexcept
{
    if (out == nullptr || (message == nullptr && message_size != 0))
        return 0;
    EC_KEY* key = test_signing_key();
    if (key == nullptr)
        return 0;

    unsigned char digest[SHA256_DIGEST_LENGTH] = {};
    SHA256(message, message_size, digest);

    SigPtr signature(
        ECDSA_do_sign(digest, SHA256_DIGEST_LENGTH, key),
        ECDSA_SIG_free);
    if (signature == nullptr)
        return 0;
    if (!normalize_low_s(signature.get()))
        return 0;

    unsigned char* cursor = out;
    const int written = i2d_ECDSA_SIG(signature.get(), &cursor);
    if (written <= 0 ||
        static_cast<std::size_t>(written) > capacity ||
        static_cast<std::size_t>(written) > kMaxDerSignatureSize)
        return 0;
    return static_cast<std::size_t>(written);
}

std::size_t loopback_tls_sign_callback_v1(void*,
                                          const std::uint8_t* message,
                                          std::size_t message_size,
                                          std::uint8_t* out,
                                          std::size_t capacity) noexcept
{
    return loopback_tls_sign_sha256_v1(message, message_size, out, capacity);
}

void loopback_tls_sign_retain_v1(void* context) noexcept
{
    if (context != nullptr)
        ++static_cast<LoopbackSignerAccountingV1*>(context)->retains;
}

void loopback_tls_sign_release_v1(void* context) noexcept
{
    if (context != nullptr)
        ++static_cast<LoopbackSignerAccountingV1*>(context)->releases;
}

} // namespace flynes::tests::nearby
