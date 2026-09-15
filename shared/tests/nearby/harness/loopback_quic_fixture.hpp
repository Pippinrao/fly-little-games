#ifndef FLYNES_TESTS_NEARBY_HARNESS_LOOPBACK_QUIC_FIXTURE_HPP
#define FLYNES_TESTS_NEARBY_HARNESS_LOOPBACK_QUIC_FIXTURE_HPP

/*
 * W3 harness: a real loopback QUIC connection through the product provider.
 *
 * This is *not* a fake transport. It drives the Rust/Quinn provider
 * (shared/nearby-quic-provider) over a real 127.0.0.1 UDP path:
 *   register TLS material -> listen -> accept -> connect -> inspect handshake
 *   -> export keying material -> bidi stream -> datagram.
 *
 * Why the TLS fixture has to exist at all. The provider pins the peer by the
 * exact 91-byte P-256 DER-SPKI and verifies the TLS 1.3 CertificateVerify with
 * ring, so the listener side must present a self-signed P-256 certificate and
 * must be able to sign the handshake transcript *at run time*. The identity is a
 * throwaway test key: it is never trusted outside this fixture and is not a
 * product secret.
 *
 * Low-S. Raw ECDSA output is not necessarily low-S; the provider rejects the
 * high-S half, so the signer normalizes s to min(s, n - s) before re-encoding
 * the DER signature. See loopback_quic_fixture.cpp.
 *
 * Platform scope. The getters below need OpenSSL's libcrypto for the runtime
 * signature, so the CMake block registers this fixture only when OpenSSL is
 * found; the Windows host build simply does not get it.
 */

#include <cstddef>
#include <cstdint>
#include <vector>

namespace flynes::tests::nearby {

/* The provider requires exactly this many bytes. */
inline constexpr std::size_t kLoopbackSpkiSizeV1 = 91;

/*
 * The fixed test identity. All three values are DER; the certificate carries
 * exactly the SPKI below, which is what the peer pin compares against.
 */
struct LoopbackTlsIdentityV1 final
{
    std::vector<std::uint8_t> private_key_der; /* PKCS#8, test-only */
    std::vector<std::uint8_t> certificate_der; /* self-signed end entity */
    std::vector<std::uint8_t> spki_der;        /* exactly 91 bytes */
};

const LoopbackTlsIdentityV1& loopback_tls_identity_v1();

/* SHA-256 of the SPKI: the pin the connector passes to the provider. */
std::vector<std::uint8_t> loopback_spki_pin_v1();

/*
 * ECDSA P-256 / SHA-256 signature over `message`, low-S normalized, DER
 * encoded. Returns the byte count written, or 0 on failure. `capacity` must be
 * at least 80 bytes (the provider's own upper bound).
 */
std::size_t loopback_tls_sign_sha256_v1(const std::uint8_t* message,
                                        std::size_t message_size,
                                        std::uint8_t* out,
                                        std::size_t capacity) noexcept;

/*
 * The exact `FlynesTlsSign` callback shape the provider expects. Register this
 * with flynes_quic_provider_register_tls_material.
 */
std::size_t loopback_tls_sign_callback_v1(void* context,
                                          const std::uint8_t* message,
                                          std::size_t message_size,
                                          std::uint8_t* out,
                                          std::size_t capacity) noexcept;

/*
 * Retain/release counters for the signer context, so the probe can assert the
 * provider balanced its references.
 */
struct LoopbackSignerAccountingV1 final
{
    int retains = 0;
    int releases = 0;
};

void loopback_tls_sign_retain_v1(void* context) noexcept;
void loopback_tls_sign_release_v1(void* context) noexcept;

} // namespace flynes::tests::nearby

#endif
