#ifndef NEARBY_QUIC_SPIKE_H
#define NEARBY_QUIC_SPIKE_H
#include <stddef.h>
#include <stdint.h>
#define NEARBY_QUIC_RESULT_CAPACITY 1024
#ifdef __cplusplus
extern "C" {
#endif
/* Blocking worker-only call, fixed 20s network deadline. Inputs are explicit-length UTF-8,
 * no trailing NUL required. bind/peer max 128 bytes; full canonical DER-SPKI pin 182 hex bytes.
 * Caller must supply readable, immutable input ranges valid throughout this call, and a
 * disjoint writable output range. Dangling pointers and foreign exceptions are forbidden.
 * Nothing borrowed escapes. Non-null nonempty output is NUL-terminated, never truncated
 * success. Capacity must be >=1024. No keys or raw exporters are returned.
 * 0=VERIFIED; 1=arguments; 2=output capacity/pointer; 3=probe/runtime; 4=caught Rust panic.
 * Rust unwinding is caught; allocator aborts and invalid caller memory cannot be recovered.
 */
int32_t nearby_quic_probe_client(const uint8_t *bind, size_t bind_length,
    const uint8_t *peer, size_t peer_length, const uint8_t *pin_hex, size_t pin_length,
    uint8_t *output, size_t output_capacity);
#ifdef __cplusplus
}
#endif
#endif
