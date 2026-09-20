#ifndef FLYNES_QUIC_PROVIDER_H
#define FLYNES_QUIC_PROVIDER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FLYNES_QUIC_PROVIDER_ABI_V1 UINT32_C(1)
#define FLYNES_QUIC_ACCEPTED INT32_C(1)
#define FLYNES_QUIC_OK INT32_C(0)
#define FLYNES_QUIC_INVALID_ARGUMENT INT32_C(-1)
#define FLYNES_QUIC_INVALID_HANDLE INT32_C(-2)
#define FLYNES_QUIC_CLOSED INT32_C(-3)
#define FLYNES_QUIC_FAILED INT32_C(-4)
#define FLYNES_QUIC_CANCELLED INT32_C(-5)
#define FLYNES_QUIC_DUPLICATE INT32_C(-6)
typedef struct FlynesQuicProvider FlynesQuicProvider;
typedef void (*FlynesContextRetain)(void*);
typedef void (*FlynesContextRelease)(void*);
typedef void (*FlynesQuicCompletion)(void*, uint64_t, int32_t, uint32_t,
    uint64_t, const uint8_t*, size_t);
typedef size_t (*FlynesTlsSign)(void*, const uint8_t*, size_t, uint8_t*, size_t);
typedef struct FlynesQuicCallbacks {
    uint32_t struct_size; uint32_t abi_version; void* context;
    FlynesContextRetain retain; FlynesContextRelease release;
    FlynesQuicCompletion completion;
} FlynesQuicCallbacks;
typedef struct FlynesTlsSignerCallbacks {
    void* context; FlynesContextRetain retain; FlynesContextRelease release;
    FlynesTlsSign sign;
} FlynesTlsSignerCallbacks;
typedef struct FlynesQuicHandshakeFactsV1 {
    uint32_t struct_size; uint32_t abi_version;
    uint16_t tls_major; uint16_t tls_minor;
    uint8_t full_handshake; uint8_t pin_verifier_invoked;
    uint8_t peer_certificate_verified; uint8_t resumed; uint8_t zero_rtt;
    uint8_t reserved_zero[3];
    uint32_t alpn_size; uint8_t alpn[32]; uint8_t peer_der_spki_hash[32];
} FlynesQuicHandshakeFactsV1;
/* Pointer inputs must be readable for the duration of the call and are copied before return.
 * Handles must be live and retain/release balanced. Callback contexts stay valid between the
 * create-time retain and final release; callback byte ranges are borrowed only for that call. */
FlynesQuicProvider* flynes_quic_provider_create(const FlynesQuicCallbacks*);
void flynes_quic_provider_retain(FlynesQuicProvider*);
void flynes_quic_provider_release(FlynesQuicProvider*);
int32_t flynes_quic_provider_register_tls_material(FlynesQuicProvider*, uint64_t,
    const uint8_t*, size_t, const uint8_t*, size_t, const FlynesTlsSignerCallbacks*);
int32_t flynes_quic_provider_generate_self_signed(FlynesQuicProvider*, uint64_t);
int32_t flynes_quic_provider_listen(FlynesQuicProvider*, uint64_t,
    const uint8_t*, size_t, uint64_t, uint64_t);
int32_t flynes_quic_provider_accept(FlynesQuicProvider*, uint64_t, uint64_t);
int32_t flynes_quic_provider_connect(FlynesQuicProvider*, uint64_t,
    /* bind, peer, and exact SHA-256(DER-SPKI) peer pin */
    const uint8_t*, size_t, const uint8_t*, size_t, const uint8_t*, size_t, uint64_t);
int32_t flynes_quic_provider_inspect_handshake(FlynesQuicProvider*, uint64_t, uint64_t);
int32_t flynes_quic_provider_exporter(FlynesQuicProvider*, uint64_t, uint64_t,
    const uint8_t*, size_t);
int32_t flynes_quic_provider_open_bidi(FlynesQuicProvider*, uint64_t, uint64_t);
int32_t flynes_quic_provider_accept_bidi(FlynesQuicProvider*, uint64_t, uint64_t);
int32_t flynes_quic_provider_open_uni(FlynesQuicProvider*, uint64_t, uint64_t);
int32_t flynes_quic_provider_write(FlynesQuicProvider*, uint64_t, uint64_t,
    const uint8_t*, size_t, uint32_t);
int32_t flynes_quic_provider_finish(FlynesQuicProvider*, uint64_t, uint64_t);
int32_t flynes_quic_provider_reset(FlynesQuicProvider*, uint64_t, uint64_t, uint32_t);
int32_t flynes_quic_provider_read(FlynesQuicProvider*, uint64_t, uint64_t, size_t);
int32_t flynes_quic_provider_grant_read_credit(FlynesQuicProvider*, uint64_t,
    uint64_t, size_t);
int32_t flynes_quic_provider_send_datagram(FlynesQuicProvider*, uint64_t, uint64_t,
    const uint8_t*, size_t);
int32_t flynes_quic_provider_read_datagram(FlynesQuicProvider*, uint64_t, uint64_t);
int32_t flynes_quic_provider_query(FlynesQuicProvider*, uint64_t, uint64_t);
int32_t flynes_quic_provider_payload_budget(FlynesQuicProvider*, uint64_t, uint64_t);
int32_t flynes_quic_provider_stats(FlynesQuicProvider*, uint64_t, uint64_t);
int32_t flynes_quic_provider_close(FlynesQuicProvider*, uint64_t, uint64_t, uint32_t);
int32_t flynes_quic_provider_cancel(FlynesQuicProvider*, uint64_t);
#ifdef __cplusplus
}
#endif
#endif
