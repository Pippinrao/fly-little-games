/*
 * W0 two-engine loopback harness (path A of the MVP-LOBBY closure).
 *
 * WHAT THIS FILE IS
 *   The port implementations two public SessionEngine instances need in order to
 *   talk to each other over a *byte-accurate* loopback: every byte that crosses
 *   from one engine to the other is produced by the sending engine's own encoders
 *   and consumed by the receiving engine's own decoders. No peer message is ever
 *   manufactured by the test.
 *
 * PROVENANCE (step 1)
 *   The port structs below are a mechanical extraction of the fixture that
 *   already shipped in tests/nearby/integration/test_two_engine_empty_lobby.cpp,
 *   because that is the only port set that matches the current ABI (it already
 *   implements the R3 object-store read that validate_object_store requires).
 *   Step 1 changes NO behaviour; later steps replace the fixed-value providers
 *   with a shared deterministic world and wire the loopback transport.
 *
 * WHAT THE MOCKS DO *NOT* PROVE
 *   The mock providers stand in for real cryptography and real transport. Their
 *   job is to exercise the protocol, the wire encoding and the pipeline. A green
 *   run here is NOT evidence that P-256 key agreement, AEAD, HMAC or QUIC work:
 *   in particular the later `agree` mock derives its shared secret from the two
 *   public keys under its own domain string, which is a stand-in for ECDH and
 *   verifies none of the actual NIST P-256 key-agreement properties. Physical
 *   transport, device behaviour and cryptographic correctness are certified
 *   elsewhere (device/host provider tests), never by this harness.
 */

#ifndef FLYNES_TESTS_NEARBY_HARNESS_TWO_ENGINE_LOOPBACK_FIXTURE_HPP
#define FLYNES_TESTS_NEARBY_HARNESS_TWO_ENGINE_LOOPBACK_FIXTURE_HPP

#include <flynes/flynes_session.h>

#include "deterministic_executor.hpp"
#include "wire/gatt_fragment.hpp"
#include "wire/initial_bearer.hpp"
#include "wire/endpoint_offer.hpp"
#include "wire/gatt_lookup_v2.hpp"
#include "wire/channel_bind.hpp"
#include "wire/p256_point.hpp"
#include "wire/pair_handshake.hpp"
#include "wire/pair_reveal.hpp"
#include "wire/pair_secure.hpp"
#include "wire/quic_contract.hpp"
#include "wire/session_signing_binding.hpp"
#include "wire/sha256.hpp"
#include "wire/app_frame.hpp"
#include "wire/link_hello.hpp"
#include "link/link_control_contract.hpp"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace flynes::session::loopback {

namespace wire = flynes::session::wire;
namespace link = flynes::session::link;

/* Defined once, in two_engine_loopback_fixture.cpp. */
extern int failures;
inline void check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

inline void retain_noop(void*) {}
inline void release_noop(void*) {}

inline std::uint64_t g_loopback_clock_ns = 1;

inline void set_loopback_clock_ns(std::uint64_t ns) noexcept
{
    g_loopback_clock_ns = ns;
}

inline void reset_loopback_clock_ns() noexcept
{
    g_loopback_clock_ns = 1;
}

inline fly_session_result_v2 read_clock(void*, fly_session_clock_sample_v2* out)
{
    if (!out) return FLY_SESSION_V2_INVALID_ARGUMENT;
    out->continuous_ns = g_loopback_clock_ns;
    out->suspend_inclusive = g_loopback_clock_ns;
    out->boot_generation[0] = 1;
    return FLY_SESSION_V2_OK;
}

[[maybe_unused]] inline fly_session_result_v2 unavailable_cancel(
    void*, const fly_session_op_token_v2*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_key_generate(
    void*, const fly_session_op_token_v2*, std::uint32_t,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_key_open(
    void*, const fly_session_op_token_v2*, std::uint32_t,
    fly_session_bytes_v2, fly_session_bytes_v2, const std::uint8_t[32],
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_key_public(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_key_sign(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, fly_session_bytes_v2, const std::uint8_t[32],
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_key_agree(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_key_release(
    void*, fly_session_resource_handle_v2)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_key_destroy(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2,
    std::uint64_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_crypto_random(
    void*, const fly_session_op_token_v2*, std::uint32_t,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_crypto_hkdf(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, std::uint32_t,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_crypto_aead(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, fly_session_bytes_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_crypto_verify(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2,
    fly_session_bytes_v2, const std::uint8_t[32], fly_session_bytes_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_crypto_hmac(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_secret_release(
    void*, fly_session_resource_handle_v2)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_tls_create(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_tls_restore(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2,
    const std::uint8_t[32], fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_tls_release(
    void*, fly_session_resource_handle_v2)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_bearer_start(
    void*, const fly_session_op_token_v2*, const std::uint8_t[32],
    fly_session_resource_handle_v2, std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_bearer_resolve(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_bearer_release(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_prepare_credential(
    void*, const fly_session_op_token_v2*, const std::uint8_t[32],
    fly_session_bytes_v2, fly_session_resource_handle_v2, std::uint32_t,
    fly_session_bytes_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_quic_start(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_resource_handle_v2,
    const fly_session_quic_connect_policy_v2*, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_quic_inspect(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_quic_exporter(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, std::uint32_t,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_quic_stream(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
/* Kept for symmetry with the rest of the unavailable_quic_* stub family; the
 * port table currently wires writes through unavailable_quic_stream, so GCC
 * flags this one as unused while MSVC does not. */
[[maybe_unused]] inline fly_session_result_v2 unavailable_quic_write(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_buffer_v2_t*, std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_quic_control(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint64_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_quic_datagram(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_resource_handle_v2, std::uint64_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_quic_query(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] inline fly_session_result_v2 unavailable_quic_close(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }

struct Platform final
{
    fly_session_op_token_v2 token{};
    fly_session_inbox_v2_t* inbox = nullptr;
    ~Platform() { fly_session_inbox_release_v2(inbox); }

    fly_session_platform_state_port_v2 port()
    {
        fly_session_platform_state_port_v2 value{};
        value.struct_size = FLY_SESSION_PLATFORM_STATE_PORT_V2_SIZE;
        value.abi_version = FLY_SESSION_ABI_VERSION_2;
        value.context = this;
        value.retain = retain_noop;
        value.release = release_noop;
        value.watch = watch;
        value.stop = stop;
        return value;
    }

    void ready() const
    {
        fly_session_port_event_v2 event{};
        event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
        event.abi_version = FLY_SESSION_ABI_VERSION_2;
        event.token = token;
        event.event_sequence = 1;
        event.event_kind = FLY_SESSION_PORT_EVENT_PLATFORM_STATE_V2;
        event.terminal = 0;
        event.result = FLY_SESSION_V2_OK;
        event.payload_kind = FLY_SESSION_PLATFORM_STATE_SNAPSHOT_V2;
        fly_session_platform_state_event_v2 payload{};
        payload.struct_size = FLY_SESSION_PLATFORM_STATE_EVENT_V2_SIZE;
        payload.abi_version = FLY_SESSION_ABI_VERSION_2;
        payload.state_revision = 1;
        payload.foreground = 1;
        payload.network_ready = 1;
        event.payload_size = sizeof(payload);
        std::memcpy(event.payload, &payload, sizeof(payload));
        check(fly_session_deliver_v2(inbox, &event) == FLY_SESSION_V2_ACCEPTED,
              "platform ready completion queues");
    }

private:
    static fly_session_result_v2 watch(void* context,
                                       const fly_session_op_token_v2* token,
                                       fly_session_inbox_v2_t* inbox)
    {
        auto* self = static_cast<Platform*>(context);
        self->token = *token;
        self->inbox = inbox;
        fly_session_inbox_retain_v2(inbox);
        return FLY_SESSION_V2_ACCEPTED;
    }
    static fly_session_result_v2 stop(void*, const fly_session_op_token_v2*)
    {
        return FLY_SESSION_V2_OK;
    }
};

/* ------------------------------------------------------------------------- *
 * Step 2: the shared deterministic provider world.
 *
 * Both engines are driven by ONE world instance, because the two sides must end
 * up with the same pair state. Every operation here is a pure function of its
 * inputs (plus a per-world counter for `random`), so an operation performed on
 * one side and the matching operation on the other side agree exactly.
 *
 * WHAT THIS WORLD DOES *NOT* PROVE
 *   It is a stand-in for real cryptography and it verifies protocol, wire
 *   encoding and pipeline behaviour only. In particular `agree` below is an ECDH
 *   STAND-IN: it derives a shared secret from the two public keys under its own
 *   domain string. It does NOT implement or check any NIST P-256 key-agreement
 *   property (no scalar multiplication, no point validation beyond the repository
 *   curve check, no contributory-behaviour guarantee). A green run of any test
 *   using this world is never evidence that ECDH, AEAD, HMAC, signatures or
 *   transport work.
 * ------------------------------------------------------------------------- */

/* Which engine an operation belongs to. Used so that the two sides get different
 * deterministic randomness; equal random bytes on both sides would hide real
 * defects instead of exposing them. */
enum class LoopbackSide : std::uint8_t
{
    Initiator = 1,
    Responder = 2
};

inline constexpr std::size_t kLoopbackPointCount = 12;
using LoopbackPoint = std::array<std::uint8_t, 65>;

/* Distinct, on-curve NIST P-256 uncompressed points, k*G for k = 1..12. The
 * fixture test re-validates every one of them with the repository's own
 * wire::validate_p256_uncompressed_point and pins the first two against the
 * constants the repository already ships, so this table cannot silently rot. */
const std::array<LoopbackPoint, kLoopbackPointCount>& loopback_p256_points() noexcept;

/* Real HMAC-SHA256 over the repository's sha256. */
std::array<std::uint8_t, 32> loopback_hmac_sha256(
    const std::uint8_t* key, std::size_t key_size,
    const std::uint8_t* input, std::size_t input_size) noexcept;

struct LoopbackWorld final
{
    /* A handle is opaque to the engine but meaningful here: it carries either a
     * public point (a key), a secret (an agreement result or a derived key), or
     * both. */
    struct Entry final
    {
        fly_session_resource_handle_v2 handle = 0;
        bool has_point = false;
        LoopbackPoint point{};
        std::vector<std::uint8_t> secret{};
    };

    /* Key material. */
    fly_session_resource_handle_v2 allocate_point(std::size_t point_index);
    fly_session_resource_handle_v2 allocate_secret(
        const std::vector<std::uint8_t>& secret);
    void release(fly_session_resource_handle_v2 handle) noexcept;
    [[nodiscard]] const Entry* find(
        fly_session_resource_handle_v2 handle) const noexcept;
    [[nodiscard]] const LoopbackPoint* point_of(
        fly_session_resource_handle_v2 handle) const noexcept;
    [[nodiscard]] const std::vector<std::uint8_t>* secret_of(
        fly_session_resource_handle_v2 handle) const noexcept;

    /*
     * ECDH STAND-IN, NOT ECDH.
     *
     * See the section comment above: this is sha256 over the two public keys in a
     * fixed order under a private domain string. It gives both sides the same
     * secret, which is all the protocol layer needs, and it deliberately does not
     * pretend to be curve arithmetic. Do not read anything about P-256 key
     * agreement out of a test that passes through here.
     */
    std::vector<std::uint8_t> agree(const LoopbackPoint& left,
                                    const LoopbackPoint& right) const;

    /* Deterministic derivations. Equal inputs always give equal output, which is
     * what lets the two engines agree without exchanging these values. */
    std::vector<std::uint8_t> hkdf(const std::vector<std::uint8_t>& secret,
                                   const std::uint8_t* salt,
                                   std::size_t salt_size,
                                   const std::uint8_t* info,
                                   std::size_t info_size,
                                   std::size_t size) const;
    std::array<std::uint8_t, 32> hmac(
        const std::vector<std::uint8_t>& key, const std::uint8_t* input,
        std::size_t input_size) const;

    /* A faithful round trip: plaintext followed by a tag bound to
     * (key, nonce, aad, plaintext). Opening with any other key fails, which is
     * what makes a cross-key mistake visible rather than silent. */
    std::vector<std::uint8_t> seal(const std::vector<std::uint8_t>& key,
                                   const std::uint8_t* nonce,
                                   std::size_t nonce_size,
                                   const std::uint8_t* aad, std::size_t aad_size,
                                   const std::uint8_t* input,
                                   std::size_t input_size) const;
    bool open(const std::vector<std::uint8_t>& key, const std::uint8_t* nonce,
              std::size_t nonce_size, const std::uint8_t* aad,
              std::size_t aad_size, const std::uint8_t* input,
              std::size_t input_size, std::vector<std::uint8_t>* out) const;

    /* Deterministic canonical low-S signature, so the wire codecs' canonicality
     * checks are exercised for real rather than bypassed. */
    std::array<std::uint8_t, 64> sign(const LoopbackPoint& key,
                                      const std::uint8_t* domain,
                                      std::size_t domain_size,
                                      const std::uint8_t digest[32]) const;
    bool verify(const LoopbackPoint& key, const std::uint8_t* domain,
                std::size_t domain_size, const std::uint8_t digest[32],
                const std::uint8_t signature[64]) const;

    /* Counter-derived, and different per side on purpose. */
    std::vector<std::uint8_t> random(LoopbackSide side, std::size_t size,
                                     const std::uint8_t* purpose,
                                     std::size_t purpose_size);

    std::vector<Entry> entries{};
    fly_session_resource_handle_v2 next_handle = 0x1000;
    std::uint64_t random_counter = 0;
};

/* Self-contained terminal builders, declared before the fixture so the port
 * callbacks below can complete an operation synchronously instead of relying on
 * a test-side script. */
void loopback_deliver_buffer(fly_session_inbox_v2_t* inbox,
                             const fly_session_op_token_v2& token,
                             std::uint32_t kind, const std::uint8_t* bytes,
                             std::size_t size);
void loopback_deliver_resource(fly_session_inbox_v2_t* inbox,
                               const fly_session_op_token_v2& token,
                               std::uint32_t kind,
                               fly_session_resource_handle_v2 resource);
void loopback_deliver_end(fly_session_inbox_v2_t* inbox,
                          const fly_session_op_token_v2& token,
                          std::uint32_t kind, fly_session_result_v2 result);
void loopback_deliver_verification(fly_session_inbox_v2_t* inbox,
                                   const fly_session_op_token_v2& token,
                                   fly_session_result_v2 result);

class LoopbackTransport;

/*
 * The provider clock, one per engine.
 *
 * This harness used to answer every clock read with the constant 1, which makes
 * a case about ELAPSED TIME inexpressible: a link interruption that lasts two
 * seconds and a link that was never interrupted produced byte-identical clock
 * samples. The value is now per-engine and a test can advance it, so the
 * recovery matrix can state how much time passed. It stays a pure function of
 * that value - no wall clock, no randomness - and the default is the old
 * constant, so every existing case is unchanged.
 */
struct ClockFixtureV1 final
{
    std::uint64_t continuous_ns = 1;
    std::uint64_t suspend_inclusive_ns = 1;
    int reads = 0;

    /* Advance BOTH clocks by `milliseconds`, the way a real clock does. */
    void advance_ms(std::uint64_t milliseconds) noexcept
    {
        continuous_ns += milliseconds * UINT64_C(1000000);
        suspend_inclusive_ns += milliseconds * UINT64_C(1000000);
    }

    static fly_session_result_v2 read(void* context,
                                      fly_session_clock_sample_v2* out)
    {
        if (context == nullptr || out == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        auto* self = static_cast<ClockFixtureV1*>(context);
        ++self->reads;
        out->continuous_ns = self->continuous_ns;
        out->suspend_inclusive = self->suspend_inclusive_ns;
        out->boot_generation[0] = 1;
        return FLY_SESSION_V2_OK;
    }
};

struct EngineFixture final
{
    struct Key final
    {
        int generates = 0;
        int public_reads = 0;
        int agreements = 0;
        int signs = 0;
        int releases = 0;
        int cancels = 0;
        fly_session_result_v2 cancel_result = FLY_SESSION_V2_OK;
        std::uint32_t last_purpose = 0;
        std::uint32_t last_encoding = 0;
        fly_session_resource_handle_v2 last_resource = 0;
        std::vector<std::uint8_t> last_binding;
        /* The peer's X9.63 point, as handed to `agree`. The pump needs it to answer
         * the key-agreement operation after the callback returned. */
        std::vector<std::uint8_t> last_peer;
        std::vector<std::uint8_t> last_domain;
        std::array<std::uint8_t, 32> last_digest{};
        fly_session_op_token_v2 last_token{};
        fly_session_inbox_v2_t* inbox = nullptr;
        /* Step 2/4: key material comes from the shared world, but the CALLBACK never
         * completes its own operation. A terminal delivered from inside a provider
         * callback is refused by the engine, and because the refusal is silent the
         * operation would stay pending forever. The callback therefore only records
         * the request and answers ACCEPTED; `pump_once` hands the completion out
         * afterwards, driven purely by this struct's own counters.
         *
         * The point index is derived from the purpose and the side so that every key
         * in the pair is a *different* on-curve point — the wire codecs require e.g.
         * the long-term identity key and the session signing key to differ. */
        LoopbackWorld* world = nullptr;
        LoopbackSide side = LoopbackSide::Initiator;

        /*
         * Step 5: like the crypto port, this port can have several operations in
         * flight at once (the session signing key is signed while the link
         * handshake also reads a public key), so the `last_*` mirrors cannot
         * identify WHICH request is being answered. Every request is recorded with
         * its own token and arguments, per kind and in order.
         */
        struct Request final
        {
            fly_session_op_token_v2 token{};
            fly_session_resource_handle_v2 resource = 0;
            std::uint32_t purpose = 0;
            std::uint32_t encoding = 0;
            std::vector<std::uint8_t> peer;
            std::vector<std::uint8_t> domain;
            std::array<std::uint8_t, 32> digest{};
        };
        std::vector<Request> generate_requests;
        std::vector<Request> public_requests;
        std::vector<Request> agree_requests;
        std::vector<Request> sign_requests;

        static std::size_t point_index_for(std::uint32_t purpose,
                                           LoopbackSide side)
        {
            std::size_t slot = 0;
            switch (purpose)
            {
            case FLY_SESSION_KEY_DEVICE_IDENTITY_V2: slot = 0; break;
            case FLY_SESSION_KEY_PAIR_ECDH_V2: slot = 1; break;
            case FLY_SESSION_KEY_TLS_V2: slot = 2; break;
            case FLY_SESSION_KEY_SESSION_SIGNING_V2: slot = 3; break;
            default: slot = 4; break;
            }
            return slot * 2 + (side == LoopbackSide::Responder ? 1u : 0u);
        }

        ~Key() { fly_session_inbox_release_v2(inbox); }

        static fly_session_result_v2 generate(
            void* context, const fly_session_op_token_v2* token,
            std::uint32_t purpose, fly_session_bytes_v2 binding,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Key*>(context);
            ++self->generates;
            self->last_purpose = purpose;
            self->last_token = *token;
            self->last_binding.assign(binding.data,
                                      binding.data + binding.size);
            Request request{};
            request.token = *token;
            request.purpose = purpose;
            self->generate_requests.push_back(std::move(request));
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 cancel(
            void* context, const fly_session_op_token_v2* token)
        {
            auto* self = static_cast<Key*>(context);
            if (std::memcmp(token, &self->last_token, sizeof(*token)) != 0)
                return FLY_SESSION_V2_STALE;
            ++self->cancels;
            return self->cancel_result;
        }

        static fly_session_result_v2 public_key(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 resource, std::uint32_t encoding,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Key*>(context);
            ++self->public_reads;
            self->last_token = *token;
            self->last_resource = resource;
            self->last_encoding = encoding;
            Request request{};
            request.token = *token;
            request.resource = resource;
            request.encoding = encoding;
            self->public_requests.push_back(std::move(request));
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 agree(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 resource,
            fly_session_bytes_v2 peer, fly_session_bytes_v2 binding,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Key*>(context);
            if (resource == 0 || peer.size != 65 || binding.size != 32)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->agreements;
            self->last_token = *token;
            self->last_resource = resource;
            self->last_peer.assign(peer.data, peer.data + peer.size);
            self->last_binding.assign(binding.data, binding.data + binding.size);
            Request request{};
            request.token = *token;
            request.resource = resource;
            request.peer = self->last_peer;
            self->agree_requests.push_back(std::move(request));
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            /* ECDH STAND-IN, NOT ECDH: see LoopbackWorld::agree. `pump_once` derives
             * the secret from the two public keys; it exercises no NIST P-256
             * key-agreement property. */
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 sign(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 resource, std::uint32_t purpose,
            fly_session_bytes_v2 domain, const std::uint8_t digest[32],
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Key*>(context);
            /* Two purposes legitimately reach this port in this fixture: the
             * long-term device identity key (pair transcript) and the session
             * signing key minted by SessionSigningScheduler, which is what
             * LINK_HELLO/READY/ACK are signed with. Anything else is a bug. */
            if (resource == 0 ||
                (purpose != FLY_SESSION_KEY_DEVICE_IDENTITY_V2 &&
                 purpose != FLY_SESSION_KEY_SESSION_SIGNING_V2) ||
                !digest)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->signs;
            self->last_token = *token;
            self->last_resource = resource;
            self->last_purpose = purpose;
            self->last_domain.assign(domain.data, domain.data + domain.size);
            std::copy_n(digest, self->last_digest.size(),
                        self->last_digest.begin());
            Request request{};
            request.token = *token;
            request.resource = resource;
            request.purpose = purpose;
            request.domain = self->last_domain;
            request.digest = self->last_digest;
            self->sign_requests.push_back(std::move(request));
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 release_key(
            void* context, fly_session_resource_handle_v2 resource)
        {
            auto* self = static_cast<Key*>(context);
            ++self->releases;
            if (self->world != nullptr) self->world->release(resource);
            return FLY_SESSION_V2_OK;
        }
    } key;

    struct Crypto final
    {
        int randoms = 0;
        int hkdfs = 0;
        int aead_seals = 0;
        int aead_opens = 0;
        int verifies = 0;
        int hmacs = 0;
        int cancels = 0;
        int releases = 0;
        std::uint32_t last_size = 0;
        /* The sizes of the individual requests. `last_size` alone is ambiguous: a
         * `random` and an `hkdf` can be in flight together, and answering each with
         * the other's size would be a completion for a request nobody made. */
        std::uint32_t last_random_size = 0;
        std::uint32_t last_hkdf_size = 0;
        /* The purpose bytes handed to `random`; the world derives its bytes from them,
         * so the pump needs the exact request to answer it. */
        std::vector<std::uint8_t> last_purpose;
        fly_session_resource_handle_v2 last_resource = 0;
        std::vector<std::uint8_t> last_salt;
        std::vector<std::uint8_t> last_info;
        std::vector<std::uint8_t> last_nonce;
        std::vector<std::uint8_t> last_aad;
        std::vector<std::uint8_t> last_input;
        std::vector<std::uint8_t> last_public_key;
        std::vector<std::uint8_t> last_signature;
        std::array<std::uint8_t, 32> last_digest{};
        fly_session_op_token_v2 last_token{};
        fly_session_inbox_v2_t* inbox = nullptr;
        /*
         * Step 5: several operations on this port can be IN FLIGHT AT ONCE - one
         * per scheduler that dispatched an effect - and the engine routes each
         * completion by the token it was dispatched with. The `last_*` mirrors are
         * therefore not enough on their own: a later callback of another kind
         * overwrites them, and the pump would then complete an earlier request
         * with a later request's bytes and token. The engine reports that as
         * CONTRACT_VIOLATION (-15), not as a wrong answer.
         *
         * Every request is therefore recorded in its own kind's FIFO, together
         * with the token and the exact arguments it was made with, and the pump
         * answers each kind strictly in request order.
         */
        struct Request final
        {
            fly_session_op_token_v2 token{};
            fly_session_resource_handle_v2 resource = 0;
            std::uint32_t size = 0;
            std::vector<std::uint8_t> purpose;
            std::vector<std::uint8_t> salt;
            std::vector<std::uint8_t> info;
            std::vector<std::uint8_t> nonce;
            std::vector<std::uint8_t> aad;
            std::vector<std::uint8_t> input;
            std::vector<std::uint8_t> public_key;
            std::vector<std::uint8_t> signature;
            std::array<std::uint8_t, 32> digest{};
        };
        std::vector<Request> random_requests;
        std::vector<Request> hkdf_requests;
        std::vector<Request> hmac_requests;
        std::vector<Request> verify_requests;
        std::vector<Request> seal_requests;
        std::vector<Request> open_requests;
        /* Step 2/4: every operation below is performed by the shared deterministic
         * world, but `pump_once` owns the completion: a provider callback may not
         * complete its own operation (the engine refuses a terminal delivered from
         * inside the callback, silently, leaving it pending forever). */
        LoopbackWorld* world = nullptr;
        LoopbackSide side = LoopbackSide::Initiator;
        ~Crypto() { fly_session_inbox_release_v2(inbox); }

        static fly_session_result_v2 random(
            void* context, const fly_session_op_token_v2* token,
            std::uint32_t size, fly_session_bytes_v2 purpose,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Crypto*>(context);
            ++self->randoms;
            self->last_size = size;
            self->last_random_size = size;
            self->last_token = *token;
            self->last_purpose.assign(purpose.data, purpose.data + purpose.size);
            Request request{};
            request.token = *token;
            request.size = size;
            request.purpose = self->last_purpose;
            self->random_requests.push_back(std::move(request));
            self->capture(inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 hkdf(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 secret, fly_session_bytes_v2 salt,
            fly_session_bytes_v2 info, std::uint32_t size,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Crypto*>(context);
            ++self->hkdfs;
            self->last_token = *token;
            self->last_resource = secret;
            self->last_size = size;
            self->last_hkdf_size = size;
            self->last_salt.assign(salt.data, salt.data + salt.size);
            self->last_info.assign(info.data, info.data + info.size);
            Request request{};
            request.token = *token;
            request.resource = secret;
            request.size = size;
            request.salt = self->last_salt;
            request.info = self->last_info;
            self->hkdf_requests.push_back(std::move(request));
            self->capture(inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 seal(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 key, fly_session_bytes_v2 nonce,
            fly_session_bytes_v2 aad, fly_session_bytes_v2 input,
            fly_session_inbox_v2_t* inbox)
        { return aead(context, token, key, nonce, aad, input, inbox, true); }

        static fly_session_result_v2 open(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 key, fly_session_bytes_v2 nonce,
            fly_session_bytes_v2 aad, fly_session_bytes_v2 input,
            fly_session_inbox_v2_t* inbox)
        { return aead(context, token, key, nonce, aad, input, inbox, false); }

        static fly_session_result_v2 release_secret(
            void* context, fly_session_resource_handle_v2 secret)
        {
            if (secret == 0) return FLY_SESSION_V2_INVALID_ARGUMENT;
            auto* self = static_cast<Crypto*>(context);
            ++self->releases;
            if (self->world != nullptr) self->world->release(secret);
            return FLY_SESSION_V2_OK;
        }

        static fly_session_result_v2 verify(
            void* context, const fly_session_op_token_v2* token,
            fly_session_bytes_v2 public_key, fly_session_bytes_v2 domain,
            const std::uint8_t digest[32], fly_session_bytes_v2 signature,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Crypto*>(context);
            if (!digest || public_key.size != 65 || signature.size != 64)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->verifies;
            self->last_token = *token;
            self->last_public_key.assign(
                public_key.data, public_key.data + public_key.size);
            self->last_info.assign(domain.data, domain.data + domain.size);
            self->last_signature.assign(
                signature.data, signature.data + signature.size);
            std::copy_n(digest, self->last_digest.size(),
                        self->last_digest.begin());
            Request request{};
            request.token = *token;
            request.public_key = self->last_public_key;
            request.info = self->last_info;
            request.signature = self->last_signature;
            request.digest = self->last_digest;
            self->verify_requests.push_back(std::move(request));
            self->capture(inbox);
            /* The mock verifier really recomputes the signature instead of accepting
             * anything, so a tampered signature is still caught; the recomputation
             * happens in `pump_once`, which then reports OK or AUTH_FAILED. */
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 hmac(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 key, fly_session_bytes_v2 input,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Crypto*>(context);
            if (key == 0 || input.size == 0)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->hmacs;
            self->last_token = *token;
            self->last_resource = key;
            self->last_input.assign(input.data, input.data + input.size);
            Request request{};
            request.token = *token;
            request.resource = key;
            request.input = self->last_input;
            self->hmac_requests.push_back(std::move(request));
            self->capture(inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 cancel(
            void* context, const fly_session_op_token_v2*)
        {
            ++static_cast<Crypto*>(context)->cancels;
            return FLY_SESSION_V2_OK;
        }

    private:
        static fly_session_result_v2 aead(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 key, fly_session_bytes_v2 nonce,
            fly_session_bytes_v2 aad, fly_session_bytes_v2 input,
            fly_session_inbox_v2_t* inbox, bool sealing)
        {
            auto* self = static_cast<Crypto*>(context);
            if (sealing) ++self->aead_seals; else ++self->aead_opens;
            self->last_token = *token;
            self->last_resource = key;
            self->last_nonce.assign(nonce.data, nonce.data + nonce.size);
            self->last_aad.assign(aad.data, aad.data + aad.size);
            self->last_input.assign(input.data, input.data + input.size);
            Request request{};
            request.token = *token;
            request.resource = key;
            request.nonce = self->last_nonce;
            request.aad = self->last_aad;
            request.input = self->last_input;
            if (sealing) self->seal_requests.push_back(std::move(request));
            else self->open_requests.push_back(std::move(request));
            self->capture(inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }

        void capture(fly_session_inbox_v2_t* value)
        {
            fly_session_inbox_retain_v2(value);
            fly_session_inbox_release_v2(inbox);
            inbox = value;
        }
    } crypto;

    struct Tls final
    {
        int creates = 0;
        int releases = 0;
        fly_session_resource_handle_v2 last_key = 0;
        /*
         * The SPKI hash this provider reported for the TLS material it minted:
         * sha256 of the 65-byte public point the shared world holds for
         * `last_key`, which is exactly the hash the engine is required to answer
         * its ping with. The link reads it back to build the connector's
         * handshake facts, so the value the connector is told it observed is the
         * listener's real SPKI hash instead of a value this harness imagined.
         */
        std::array<std::uint8_t, 32> last_spki_hash{};
        fly_session_op_token_v2 last_token{};
        fly_session_inbox_v2_t* inbox = nullptr;
        ~Tls() { fly_session_inbox_release_v2(inbox); }

        static fly_session_result_v2 create(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 key, fly_session_bytes_v2,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Tls*>(context);
            ++self->creates;
            self->last_key = key;
            self->last_token = *token;
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 release_material(
            void* context, fly_session_resource_handle_v2)
        {
            ++static_cast<Tls*>(context)->releases;
            return FLY_SESSION_V2_OK;
        }
    } tls;

    struct Bearer final
    {
        int probes = 0;
        int creates = 0;
        int joins = 0;
        int prepares = 0;
        int resolves = 0;
        int cancels = 0;
        fly_session_result_v2 cancel_result = FLY_SESSION_V2_ACCEPTED;
        /*
         * W3 tamper matrix (default off). When set, the credential THIS provider
         * mints is encoded from canonical join parameters with one field byte
         * XORed. The credential therefore no longer matches the plan both ends
         * agreed on, and the receiving engine's own byte-for-byte comparison of
         * the canonical join parameters is what has to reject it.
         */
        bool tamper_join_params = false;
        std::uint8_t tamper_join_params_mask = 0x01;
        int join_params_tampered = 0;
        fly_session_op_token_v2 last_token{};
        std::vector<std::uint8_t> last_policy;
        std::vector<std::uint8_t> last_plan;
        std::vector<std::uint8_t> last_join_params;
        fly_session_resource_handle_v2 last_resource = 0;
        std::uint32_t last_confirmation_budget = 0;
        bool last_creator = false;
        fly_session_inbox_v2_t* inbox = nullptr;

        ~Bearer() { fly_session_inbox_release_v2(inbox); }

        static fly_session_result_v2 probe(
            void* context, const fly_session_op_token_v2* token,
            fly_session_bytes_v2 policy, std::uint64_t,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Bearer*>(context);
            ++self->probes;
            self->last_token = *token;
            self->last_policy.assign(policy.data, policy.data + policy.size);
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 cancel(
            void* context, const fly_session_op_token_v2* token)
        {
            auto* self = static_cast<Bearer*>(context);
            if (std::memcmp(token, &self->last_token, sizeof(*token)) != 0)
                return FLY_SESSION_V2_STALE;
            ++self->cancels;
            return self->cancel_result;
        }

        static fly_session_result_v2 start(
            void* context, const fly_session_op_token_v2* token,
            const std::uint8_t*, fly_session_resource_handle_v2 credential,
            std::uint32_t confirmation_budget, fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Bearer*>(context);
            if (credential == 0) ++self->creates; else ++self->joins;
            self->last_token = *token;
            self->last_resource = credential;
            self->last_confirmation_budget = confirmation_budget;
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 prepare(
            void* context, const fly_session_op_token_v2* token,
            const std::uint8_t*, fly_session_bytes_v2 selected_plan,
            fly_session_resource_handle_v2 bearer, std::uint32_t creator,
            fly_session_bytes_v2 canonical_join_params,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Bearer*>(context);
            if (selected_plan.size != 48 || creator > 1 ||
                (creator == 1 && bearer == 0))
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->prepares;
            self->last_token = *token;
            self->last_resource = bearer;
            self->last_creator = creator == 1;
            self->last_plan.assign(selected_plan.data,
                                   selected_plan.data + selected_plan.size);
            self->last_join_params.assign(
                canonical_join_params.data,
                canonical_join_params.data + canonical_join_params.size);
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 resolve(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 path,
            fly_session_bytes_v2 listener_token, fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Bearer*>(context);
            if (path == 0) return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->resolves;
            self->last_token = *token;
            self->last_resource = path;
            self->last_join_params.assign(
                listener_token.data, listener_token.data + listener_token.size);
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            return FLY_SESSION_V2_ACCEPTED;
        }
    } bearer;

    struct Discovery final
    {
        int scans = 0;
        int advertisements = 0;
        int connects = 0;
        int stops = 0;
        int subscriptions = 0;
        int writes = 0;
        int physical_acks = 0;
        int disconnects = 0;
        fly_session_resource_handle_v2 last_candidate = 0;
        std::uint64_t last_candidate_generation = 0;
        bool asynchronous_stop = false;
        fly_session_op_token_v2 last_token{};
        fly_session_op_token_v2 write_token{};
        /*
         * The token the engine subscribed with. A class-2 DISCOVERY_BYTES event is
         * accepted only when its token EQUALS the engine's own subscription token
         * byte for byte; `last_token` is overwritten by every later scan, write or
         * subscribe call and would be judged STALE (-3).
         */
        fly_session_op_token_v2 subscription_token{};
        std::vector<std::vector<std::uint8_t>> written_fragments;
        /*
         * The physical acknowledgements this end wrote. They were previously thrown
         * away: the writer of a logical message stops half-way through it while the
         * acknowledgement of what it already sent is outstanding, so without carrying
         * these fragments back to the peer the exchange stalls mid-message.
         */
        std::vector<std::vector<std::uint8_t>> ack_fragments;
        /*
         * Every write token this port was handed, in order. The public token is
         * what identifies an operation to a provider, and the engine routes a
         * completion by (operation_id, event_sequence) alone
         * (session_engine.cpp:2387-2399); a test therefore needs the engine's own
         * write tokens to show that it never hands one operation id to two
         * different requests.
         */
        std::vector<fly_session_op_token_v2> write_tokens;
        fly_session_inbox_v2_t* inbox = nullptr;

        ~Discovery() { fly_session_inbox_release_v2(inbox); }

        fly_session_discovery_port_v2 port()
        {
            fly_session_discovery_port_v2 value{};
            value.struct_size = FLY_SESSION_DISCOVERY_PORT_V2_SIZE;
            value.abi_version = FLY_SESSION_ABI_VERSION_2;
            value.context = this;
            value.retain = retain_noop;
            value.release = release_noop;
            value.scan = scan;
            value.advertise = advertise;
            value.stop = stop;
            value.connect = connection;
            value.disconnect = disconnect;
            value.write = write;
            value.indicate = write;
            value.subscribe = subscribe;
            return value;
        }

    private:
        static fly_session_result_v2 scan(
            void* context, const fly_session_op_token_v2* token,
            fly_session_bytes_v2, std::uint64_t, fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Discovery*>(context);
            ++self->scans; self->last_token = *token;
            self->capture(inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }
        static fly_session_result_v2 advertise(
            void* context, const fly_session_op_token_v2* token,
            fly_session_bytes_v2, std::uint64_t, fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Discovery*>(context);
            ++self->advertisements; self->last_token = *token;
            self->capture(inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }
        static fly_session_result_v2 stop(void* context,
                                          const fly_session_op_token_v2*)
        {
            auto* self = static_cast<Discovery*>(context);
            ++self->stops;
            return self->asynchronous_stop ? FLY_SESSION_V2_ACCEPTED
                                           : FLY_SESSION_V2_OK;
        }
        static fly_session_result_v2 connection(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 candidate,
            std::uint64_t generation, fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Discovery*>(context);
            ++self->connects;
            self->last_token = *token;
            self->last_candidate = candidate;
            self->last_candidate_generation = generation;
            self->capture(inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }
        static fly_session_result_v2 disconnect(
            void* context, const fly_session_op_token_v2*,
            fly_session_resource_handle_v2, std::uint64_t,
            fly_session_inbox_v2_t*)
        {
            ++static_cast<Discovery*>(context)->disconnects;
            return FLY_SESSION_V2_OK;
        }
        static fly_session_result_v2 write(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 connection,
            std::uint32_t characteristic, fly_session_buffer_v2_t* buffer,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Discovery*>(context);
            if (connection == 0 || buffer == nullptr ||
                (characteristic != FLY_SESSION_DISCOVERY_WRITE_V2 &&
                 characteristic != FLY_SESSION_DISCOVERY_INDICATE_V2))
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            std::uint64_t size = 0;
            if (fly_session_buffer_size_v2(buffer, &size) != FLY_SESSION_V2_OK ||
                size == 0 || size > 517)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
            std::uint64_t written = 0;
            const fly_session_write_bytes_v2 destination{
                bytes.data(), bytes.size()};
            if (fly_session_buffer_read_v2(
                    buffer, 0, destination, &written) != FLY_SESSION_V2_OK ||
                written != size)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            self->write_token = *token;
            self->write_tokens.push_back(*token);
            if (bytes.size() >= 2 && bytes[1] == static_cast<std::uint8_t>(
                    flynes::session::wire::GattLogicalType::PhysicalAck))
            {
                ++self->physical_acks;
                self->ack_fragments.push_back(std::move(bytes));
                return FLY_SESSION_V2_OK;
            }
            else
            {
                ++self->writes;
                self->written_fragments.push_back(std::move(bytes));
            }
            self->capture(inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }
        static fly_session_result_v2 subscribe(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 connection,
            std::uint32_t characteristic, fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Discovery*>(context);
            if (connection == 0 ||
                (characteristic != FLY_SESSION_DISCOVERY_WRITE_V2 &&
                 characteristic != FLY_SESSION_DISCOVERY_INDICATE_V2))
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->subscriptions;
            self->last_token = *token;
            self->subscription_token = *token;
            self->capture(inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }

        void capture(fly_session_inbox_v2_t* value)
        {
            if (inbox == value) return;
            fly_session_inbox_retain_v2(value);
            fly_session_inbox_release_v2(inbox);
            inbox = value;
        }
    } discovery;

    struct Quic final
    {
        int listens = 0;
        int connects = 0;
        int inspections = 0;
        int exporters = 0;
        int accepted_bidi = 0;
        int opened_bidi = 0;
        int rom_streams = 0;
        std::uint32_t last_stream_kind = 0;
        int writes = 0;
        int reads = 0;
        int cancels = 0;
        fly_session_result_v2 cancel_result = FLY_SESSION_V2_OK;
        fly_session_op_token_v2 cancelled_token{};
        int closes = 0;
        fly_session_op_token_v2 close_token{};
        fly_session_resource_handle_v2 close_connection = 0;
        fly_session_result_v2 close_result = FLY_SESSION_V2_OK;
        fly_session_op_token_v2 last_token{};
        fly_session_resource_handle_v2 last_resource = 0;
        std::uint64_t last_credit = 0;
        std::uint32_t last_finish = 0;
        std::vector<std::uint8_t> last_endpoint;
        std::vector<std::uint8_t> last_label;
        std::vector<std::uint8_t> last_context;
        std::vector<std::uint8_t> last_write;
        /*
         * The exact SPKI pin this engine's own QUIC connect policy asked the
         * provider to enforce, captured at listen/connect time. The link reports
         * it back in a connector's handshake facts because the engine's own
         * wire::verify_quic_handshake_v2 (quic_contract.cpp:131) compares the
         * facts against the policy; any other value would be a fabricated
         * success. `last_policy_spki` is only ever the value THIS engine asked
         * for, never one the harness chose.
         */
        std::array<std::uint8_t, 32> last_policy_spki{};
        /*
         * Every stream write this engine really issued, in order, with the bytes
         * its own encoder produced and the FIN flag it asked for. The QUIC relay
         * carries these and nothing else, which is what makes the loopback
         * byte-accurate rather than scripted.
         */
        struct StreamWrite final
        {
            fly_session_op_token_v2 token{};
            fly_session_resource_handle_v2 stream = 0;
            std::vector<std::uint8_t> bytes;
            std::uint32_t finish = 0;
        };
        std::vector<StreamWrite> written_streams;
        /*
         * Every read credit this engine granted, in order. The engine accepts an
         * inbound QUIC_DATA event only under the token of the read it dispatched
         * (session_engine.cpp:2432/:2436 pick the scheduler by token and
         * parse_provider_event_v2 then compares that same token), so the relay
         * must use exactly the token recorded here - never `last_token`, which a
         * later write or stream operation has overwritten.
         */
        struct StreamRead final
        {
            fly_session_op_token_v2 token{};
            fly_session_resource_handle_v2 stream = 0;
            std::uint64_t credit = 0;
        };
        std::vector<StreamRead> granted_reads;
        fly_session_op_token_v2 last_read_token{};
        fly_session_resource_handle_v2 last_read_stream = 0;
        std::uint64_t last_read_credit = 0;
        /*
         * What this provider really reported back to the engine for the two
         * link-wide QUIC values, so a test can compare the two ends with each
         * other and with the transport instead of trusting the harness's own
         * copy. `handshakes`/`exporters` are the counts of completions DELIVERED.
         */
        int handshakes = 0;
        int exporter_results = 0;
        std::array<std::uint8_t, 32> last_handshake_hash{};
        std::array<std::uint8_t, 32> last_exporter{};
        /*
         * W3 tamper matrix (default off). When set, THIS end is handed
         * `exporter_override` instead of the link's own TLS exporter - i.e. it is
         * told a different exporter than its peer. Both engines then derive
         * different channel ids, so the ChannelBind proof/ACK can no longer
         * verify. The value is still a provider answer, never a peer byte.
         */
        bool use_exporter_override = false;
        std::array<std::uint8_t, 32> exporter_override{};
        int exporter_overrides = 0;
        fly_session_inbox_v2_t* inbox = nullptr;

        ~Quic() { fly_session_inbox_release_v2(inbox); }

        void capture(const fly_session_op_token_v2* token,
                     fly_session_resource_handle_v2 resource,
                     fly_session_inbox_v2_t* value)
        {
            last_token = *token;
            last_resource = resource;
            fly_session_inbox_retain_v2(value);
            fly_session_inbox_release_v2(inbox);
            inbox = value;
        }

        static fly_session_result_v2 start(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 path, fly_session_bytes_v2 endpoint,
            fly_session_resource_handle_v2 tls_material,
            const fly_session_quic_connect_policy_v2* policy,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Quic*>(context);
            if (!token || path == 0 || !policy ||
                policy->require_full_tls13 != 1 ||
                policy->forbid_resumption != 1 ||
                policy->forbid_zero_rtt != 1)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            if (tls_material == 0) ++self->connects;
            else ++self->listens;
            self->last_endpoint.assign(endpoint.data,
                                       endpoint.data + endpoint.size);
            std::copy_n(policy->expected_der_spki_hash, 32,
                        self->last_policy_spki.begin());
            self->capture(token, path, inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 inspect(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 connection,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Quic*>(context);
            ++self->inspections;
            self->capture(token, connection, inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 exporter(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 connection,
            fly_session_bytes_v2 label, fly_session_bytes_v2 exporter_context,
            std::uint32_t output_size, fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Quic*>(context);
            if (output_size != 32) return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->exporters;
            self->last_label.assign(label.data, label.data + label.size);
            self->last_context.assign(exporter_context.data,
                                      exporter_context.data +
                                          exporter_context.size);
            self->capture(token, connection, inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 open_bidi(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 connection, std::uint32_t,
            std::uint32_t stream_kind, fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Quic*>(context);
            if (stream_kind != 1 && stream_kind != 3 && stream_kind != 5)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->opened_bidi;
            self->last_stream_kind = stream_kind;
            if (stream_kind == 5)
                ++self->rom_streams;
            self->capture(token, connection, inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 accept_bidi(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 connection, std::uint32_t,
            std::uint32_t stream_kind, fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Quic*>(context);
            if (stream_kind != 1 && stream_kind != 3 && stream_kind != 5)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->accepted_bidi;
            self->last_stream_kind = stream_kind;
            if (stream_kind == 5)
                ++self->rom_streams;
            self->capture(token, connection, inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 write(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 stream,
            fly_session_buffer_v2_t* buffer, std::uint32_t finish,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Quic*>(context);
            std::uint64_t size = 0;
            if (!buffer || fly_session_buffer_size_v2(buffer, &size) !=
                               FLY_SESSION_V2_OK)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            self->last_write.assign(static_cast<std::size_t>(size), 0);
            std::uint64_t written = 0;
            const fly_session_write_bytes_v2 destination{
                self->last_write.data(), size};
            if (fly_session_buffer_read_v2(buffer, 0, destination, &written) !=
                    FLY_SESSION_V2_OK || written != size)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->writes;
            self->last_finish = finish;
            self->capture(token, stream, inbox);
            /* The write log is the ONLY source of bytes the QUIC relay carries:
             * the engine's own encoder output, kept verbatim with the FIN flag
             * the engine asked for. */
            StreamWrite record{};
            record.token = *token;
            record.stream = stream;
            record.bytes = self->last_write;
            record.finish = finish;
            self->written_streams.push_back(std::move(record));
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 grant_read(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 stream, std::uint64_t credit,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Quic*>(context);
            if (credit == 0) return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->reads;
            self->last_credit = credit;
            self->capture(token, stream, inbox);
            /* The token recorded here is the one the engine will accept inbound
             * bytes under; `last_token` will be overwritten by the next write or
             * stream operation long before the peer's bytes arrive. */
            self->last_read_token = *token;
            self->last_read_stream = stream;
            self->last_read_credit = credit;
            StreamRead record{};
            record.token = *token;
            record.stream = stream;
            record.credit = credit;
            self->granted_reads.push_back(std::move(record));
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 cancel(
            void* context, const fly_session_op_token_v2* token)
        {
            auto* self = static_cast<Quic*>(context);
            ++self->cancels;
            self->cancelled_token = *token;
            return self->cancel_result;
        }

        static fly_session_result_v2 close(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 connection, std::uint32_t,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Quic*>(context);
            ++self->closes;
            self->close_token = *token;
            self->close_connection = connection;
            self->capture(token, connection, inbox);
            return self->close_result;
        }
    } quic;

    struct SecureStore final
    {
        int writes = 0;
        int cancels = 0;
        fly_session_op_token_v2 last_token{};
        std::vector<std::uint8_t> last_namespace;
        std::vector<std::uint8_t> last_record_key;
        std::vector<std::uint8_t> last_value;
        std::uint64_t last_expected_revision = 0;
        fly_session_inbox_v2_t* inbox = nullptr;
        ~SecureStore() { fly_session_inbox_release_v2(inbox); }

        static fly_session_result_v2 read(
            void*, const fly_session_op_token_v2*, fly_session_bytes_v2,
            fly_session_bytes_v2, fly_session_inbox_v2_t*)
        { return FLY_SESSION_V2_UNAVAILABLE; }
        static fly_session_result_v2 compare_replace(
            void* context, const fly_session_op_token_v2* token,
            fly_session_bytes_v2 name_space, fly_session_bytes_v2 record_key,
            std::uint64_t expected_revision, fly_session_buffer_v2_t* buffer,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<SecureStore*>(context);
            if (!token || name_space.size == 0 ||
                (record_key.size != 33 && record_key.size != 17) ||
                !buffer) return FLY_SESSION_V2_INVALID_ARGUMENT;
            std::uint64_t size = 0;
            if (fly_session_buffer_size_v2(buffer, &size) != FLY_SESSION_V2_OK ||
                size == 0) return FLY_SESSION_V2_INVALID_ARGUMENT;
            self->last_value.assign(static_cast<std::size_t>(size), 0);
            std::uint64_t written = 0;
            const fly_session_write_bytes_v2 output{
                self->last_value.data(), self->last_value.size()};
            if (fly_session_buffer_read_v2(buffer, 0, output, &written) !=
                    FLY_SESSION_V2_OK || written != size)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->writes;
            self->last_token = *token;
            self->last_namespace.assign(name_space.data,
                                        name_space.data + name_space.size);
            self->last_record_key.assign(record_key.data,
                                         record_key.data + record_key.size);
            self->last_expected_revision = expected_revision;
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            return FLY_SESSION_V2_ACCEPTED;
        }
        static fly_session_result_v2 remove(
            void*, const fly_session_op_token_v2*, fly_session_bytes_v2,
            fly_session_bytes_v2, std::uint64_t, fly_session_inbox_v2_t*)
        { return FLY_SESSION_V2_UNAVAILABLE; }
        static fly_session_result_v2 cancel(
            void* context, const fly_session_op_token_v2*)
        {
            ++static_cast<SecureStore*>(context)->cancels;
            return FLY_SESSION_V2_OK;
        }
    } secure_store;

    struct ObjectStore final
    {
        int puts = 0;
        int cancels = 0;
        std::uint32_t last_kind = 0;
        // Every object kind this generation asked to persist, in order. Used to
        // prove the engine never persists a 0x0216/0x0217 control object while
        // the ABI has no object-read primitive.
        std::vector<std::uint32_t> kinds;
        std::array<std::uint8_t, 32> last_hash{};
        std::vector<std::uint8_t> last_value;
        fly_session_op_token_v2 last_token{};
        fly_session_inbox_v2_t* inbox = nullptr;
        /* R3 durable re-read gate: every read-back request this generation made,
         * with its object kind and expected content hash, plus the objects that
         * were actually persisted so a read can return the exact bytes. */
        int reads = 0;
        int missing_reads = 0;
        std::uint32_t last_read_kind = 0;
        std::array<std::uint8_t, 32> last_read_hash{};
        fly_session_op_token_v2 last_read_token{};
        fly_session_inbox_v2_t* read_inbox = nullptr;
        std::vector<std::uint32_t> read_kinds;
        struct StoredObject
        {
            std::uint32_t kind = 0;
            std::array<std::uint8_t, 32> hash{};
            std::vector<std::uint8_t> value;
        };
        std::vector<StoredObject> stored;
        ~ObjectStore()
        {
            fly_session_inbox_release_v2(inbox);
            fly_session_inbox_release_v2(read_inbox);
        }

        static fly_session_result_v2 put_immutable(
            void* context, const fly_session_op_token_v2* token,
            std::uint32_t object_kind, const std::uint8_t expected_hash[32],
            fly_session_buffer_v2_t* buffer, fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<ObjectStore*>(context);
            if (!token || object_kind == 0 || !expected_hash || !buffer)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            std::uint64_t size = 0;
            if (fly_session_buffer_size_v2(buffer, &size) != FLY_SESSION_V2_OK ||
                size == 0)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            self->last_value.assign(static_cast<std::size_t>(size), 0);
            std::uint64_t written = 0;
            const fly_session_write_bytes_v2 output{
                self->last_value.data(), self->last_value.size()};
            if (fly_session_buffer_read_v2(buffer, 0, output, &written) !=
                    FLY_SESSION_V2_OK || written != size)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->puts;
            self->last_kind = object_kind;
            self->kinds.push_back(object_kind);
            std::copy_n(expected_hash, 32, self->last_hash.begin());
            self->last_token = *token;
            StoredObject record{};
            record.kind = object_kind;
            std::copy_n(expected_hash, 32, record.hash.begin());
            record.value = self->last_value;
            self->stored.push_back(std::move(record));
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            return FLY_SESSION_V2_ACCEPTED;
        }

        /*
         * The R3 read primitive. It records the exact request and then answers
         * with the retained object for (kind, hash), or with
         * FLY_SESSION_V2_UNAVAILABLE when this store never held it. The two
         * failure modes the ABI keeps apart are therefore both reachable: an
         * absent object is UNAVAILABLE, while an object whose bytes or hash
         * disagree is injected by the test as a non-OK terminal.
         */
        static fly_session_result_v2 read(
            void* context, const fly_session_op_token_v2* token,
            std::uint32_t object_kind, const std::uint8_t expected_hash[32],
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<ObjectStore*>(context);
            if (!token || object_kind == 0 || !expected_hash)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->reads;
            self->last_read_kind = object_kind;
            std::copy_n(expected_hash, 32, self->last_read_hash.begin());
            self->last_read_token = *token;
            self->read_kinds.push_back(object_kind);
            const auto found = std::find_if(
                self->stored.begin(), self->stored.end(),
                [&](const StoredObject& item) {
                    return item.kind == object_kind &&
                           std::equal(item.hash.begin(), item.hash.end(),
                                      expected_hash);
                });
            if (found == self->stored.end())
            {
                ++self->missing_reads;
                return FLY_SESSION_V2_UNAVAILABLE;
            }
            if (inbox == nullptr) return FLY_SESSION_V2_INVALID_ARGUMENT;
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->read_inbox);
            self->read_inbox = inbox;
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 cancel(
            void* context, const fly_session_op_token_v2*)
        {
            ++static_cast<ObjectStore*>(context)->cancels;
            return FLY_SESSION_V2_OK;
        }
    } object_store;

    /*
     * Optional one-choice content catalog. Default off: engines then publish no
     * SELECT_CONTENT, matching today's empty lobby. Index 0 is the one record;
     * any other index is synchronous EMPTY (never an async result>OK event).
     */
    struct Content final
    {
        std::uint32_t item_count = 1;
        struct Request final
        {
            fly_session_op_token_v2 token{};
            std::uint32_t index = 0;
        };
        std::vector<Request> queries;
        int cancels = 0;
        fly_session_inbox_v2_t* inbox = nullptr;
        ~Content() { fly_session_inbox_release_v2(inbox); }

        static fly_session_result_v2 query(
            void* context, const fly_session_op_token_v2* token,
            std::uint32_t index, fly_session_inbox_v2_t* inbox)
        {
            if (!token || !inbox) return FLY_SESSION_V2_INVALID_ARGUMENT;
            auto* self = static_cast<Content*>(context);
            if (index >= self->item_count) return FLY_SESSION_V2_EMPTY;
            Request request{};
            request.token = *token;
            request.index = index;
            self->queries.push_back(request);
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 cancel(
            void* context, const fly_session_op_token_v2*)
        {
            ++static_cast<Content*>(context)->cancels;
            return FLY_SESSION_V2_OK;
        }
    } content;

    /*
     * Deterministic fake DualRuntimePort. Load/step/export are synchronous so a
     * 600-frame run does not need a ROM: each step mixes the canonical four-port
     * masks into a 32-byte state that export_state returns verbatim.
     */
    struct DualRuntime final
    {
        std::uint64_t frame = 0;
        std::array<std::uint8_t, 32> state{};
        fly_session_dual_content_ref_v2 last_content{};
        int loads = 0;
        int steps = 0;
        int exports = 0;

        static fly_session_result_v2 load(
            void* context, const fly_session_dual_content_ref_v2* content)
        {
            auto* self = static_cast<DualRuntime*>(context);
            ++self->loads;
            self->last_content = *content;
            self->frame = 0;
            std::memcpy(self->state.data(), content->content_hash, 32u);
            return FLY_SESSION_V2_OK;
        }

        static fly_session_result_v2 step(
            void* context, const fly_session_dual_input_bundle_v2* input,
            fly_session_dual_frame_outcome_v2* out)
        {
            auto* self = static_cast<DualRuntime*>(context);
            ++self->steps;
            for (std::uint32_t port = 0; port < FLY_SESSION_DUAL_PORT_COUNT_V2;
                 ++port)
            {
                const auto mix = static_cast<std::uint8_t>(
                    input->ports[port].mask ^
                    (input->ports[port].input_sequence & 0xffu) ^ port);
                self->state[port % 32u] =
                    static_cast<std::uint8_t>(self->state[port % 32u] + mix);
                self->state[(port + 16u) % 32u] ^= mix;
            }
            self->state[31] = static_cast<std::uint8_t>(
                self->state[31] + static_cast<std::uint8_t>(input->frame_index));
            ++self->frame;
            *out = {};
            out->frame_index = input->frame_index;
            out->honoured_port_mask = 0x3u;
            out->applied_input_sequence[0] = input->ports[0].input_sequence;
            out->applied_input_sequence[1] = input->ports[1].input_sequence;
            return FLY_SESSION_V2_OK;
        }

        static fly_session_result_v2 export_state(
            void* context, std::uint8_t* out, std::size_t capacity,
            std::size_t* out_written, std::uint8_t hash_out[32])
        {
            auto* self = static_cast<DualRuntime*>(context);
            ++self->exports;
            if (capacity < self->state.size())
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            std::memcpy(out, self->state.data(), self->state.size());
            *out_written = self->state.size();
            std::memcpy(hash_out, self->state.data(), 32u);
            return FLY_SESSION_V2_OK;
        }

        static fly_session_result_v2 import_state(
            void* context, const std::uint8_t* bytes, std::size_t size)
        {
            auto* self = static_cast<DualRuntime*>(context);
            if (size != self->state.size())
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            std::memcpy(self->state.data(), bytes, size);
            return FLY_SESSION_V2_OK;
        }

        static fly_session_result_v2 state_digest(
            void* context, std::uint64_t frame_index,
            fly_session_dual_state_digest_v2* out)
        {
            auto* self = static_cast<DualRuntime*>(context);
            *out = {};
            std::memcpy(out->state, self->state.data(), 32u);
            out->frame[0] = static_cast<std::uint8_t>(frame_index);
            return FLY_SESSION_V2_OK;
        }
    } dual_runtime;

    DeterministicExecutor executor;
    Platform platform;
    /* W3 recovery matrix: this engine's own advanceable clock. */
    ClockFixtureV1 clock_fixture{};
    fly_session_clock_port_v2 clock{};
    fly_session_executor_port_v2 executor_port{};
    fly_session_platform_state_port_v2 platform_port{};
    fly_session_discovery_port_v2 discovery_port{};
    fly_session_key_port_v2 key_port{};
    fly_session_crypto_port_v2 crypto_port{};
    fly_session_tls_material_port_v2 tls_port{};
    fly_session_secure_store_port_v2 secure_store_port{};
    fly_session_object_store_port_v2 object_store_port{};
    fly_session_bearer_port_v2 bearer_port{};
    fly_session_quic_port_v2 quic_port{};
    fly_session_content_port_v2 content_port{};
    fly_session_dual_runtime_port_v2 dual_runtime_port{};
    fly_session_ports_v2 ports{};
    fly_session_v2_t* engine = nullptr;
    /*
     * The loopback link this engine was attached to, if any. `LoopbackTransport`
     * sets it in `attach`. The QUIC port is answered from the LINK's own facts
     * (step 5: one TLS handshake, one exporter for both ends), so the pump needs
     * the link; an engine that was never attached to a transport cannot
     * legitimately reach the QUIC stage, and the pump reports that instead of
     * inventing link facts.
     */
    LoopbackTransport* attached_link = nullptr;

    explicit EngineFixture(bool secure_pairing_ports = true)
        : EngineFixture(nullptr, LoopbackSide::Initiator, secure_pairing_ports)
    {
    }

    /*
     * Step 2: the two-engine form. Both engines share ONE world so that the
     * operations they perform (key generation, agreement, derivation, sealing,
     * signing) line up exactly; `side` only affects the deterministic randomness
     * and which on-curve point a purpose maps to.
     */
    EngineFixture(LoopbackWorld& shared_world, LoopbackSide shared_side,
                  bool secure_pairing_ports = true,
                  bool enable_content = false,
                  bool enable_dual_runtime = false,
                  std::uint32_t content_items = 1)
        : EngineFixture(&shared_world, shared_side, secure_pairing_ports,
                        enable_content, enable_dual_runtime, content_items)
    {
    }

private:
    EngineFixture(LoopbackWorld* shared_world, LoopbackSide shared_side,
                  bool secure_pairing_ports, bool enable_content = false,
                  bool enable_dual_runtime = false,
                  std::uint32_t content_items = 1)
    {
        key.world = shared_world;
        key.side = shared_side;
        crypto.world = shared_world;
        crypto.side = shared_side;
        clock.struct_size = FLY_SESSION_CLOCK_PORT_V2_SIZE;
        clock.abi_version = FLY_SESSION_ABI_VERSION_2;
        clock.retain = retain_noop;
        clock.release = release_noop;
        clock.read_continuous = ClockFixtureV1::read;
        clock.context = &clock_fixture;
        executor_port = executor.port();
        platform_port = platform.port();
        discovery_port = discovery.port();
        key_port.struct_size = FLY_SESSION_KEY_PORT_V2_SIZE;
        key_port.abi_version = FLY_SESSION_ABI_VERSION_2;
        key_port.context = &key;
        key_port.retain = retain_noop;
        key_port.release = release_noop;
        key_port.generate = Key::generate;
        key_port.open = unavailable_key_open;
        key_port.public_key = Key::public_key;
        key_port.prehashed_sign = Key::sign;
        key_port.key_agree = Key::agree;
        key_port.release_key = Key::release_key;
        key_port.destroy = unavailable_key_destroy;
        key_port.cancel = Key::cancel;
        crypto_port.struct_size = FLY_SESSION_CRYPTO_PORT_V2_SIZE;
        crypto_port.abi_version = FLY_SESSION_ABI_VERSION_2;
        crypto_port.context = &crypto;
        crypto_port.retain = retain_noop;
        crypto_port.release = release_noop;
        crypto_port.random = Crypto::random;
        crypto_port.hkdf = Crypto::hkdf;
        crypto_port.aead_seal = Crypto::seal;
        crypto_port.aead_open = Crypto::open;
        crypto_port.verify_prehashed = Crypto::verify;
        crypto_port.release_secret = Crypto::release_secret;
        crypto_port.cancel = Crypto::cancel;
        crypto_port.hmac_sha256 = Crypto::hmac;
        tls_port.struct_size = FLY_SESSION_TLS_MATERIAL_PORT_V2_SIZE;
        tls_port.abi_version = FLY_SESSION_ABI_VERSION_2;
        tls_port.context = &tls;
        tls_port.retain = retain_noop;
        tls_port.release = release_noop;
        tls_port.create = Tls::create;
        tls_port.restore = unavailable_tls_restore;
        tls_port.release_material = Tls::release_material;
        tls_port.cancel = unavailable_cancel;
        secure_store_port.struct_size = FLY_SESSION_SECURE_STORE_PORT_V2_SIZE;
        secure_store_port.abi_version = FLY_SESSION_ABI_VERSION_2;
        secure_store_port.context = &secure_store;
        secure_store_port.retain = retain_noop;
        secure_store_port.release = release_noop;
        secure_store_port.read = SecureStore::read;
        secure_store_port.compare_replace = SecureStore::compare_replace;
        secure_store_port.remove = SecureStore::remove;
        secure_store_port.cancel = SecureStore::cancel;
        object_store_port.struct_size = FLY_SESSION_OBJECT_STORE_PORT_V2_SIZE;
        object_store_port.abi_version = FLY_SESSION_ABI_VERSION_2;
        object_store_port.context = &object_store;
        object_store_port.retain = retain_noop;
        object_store_port.release = release_noop;
        object_store_port.put_immutable = ObjectStore::put_immutable;
        object_store_port.read = ObjectStore::read;
        object_store_port.cancel = ObjectStore::cancel;
        bearer_port.struct_size = FLY_SESSION_BEARER_PORT_V2_SIZE;
        bearer_port.abi_version = FLY_SESSION_ABI_VERSION_2;
        bearer_port.context = &bearer;
        bearer_port.retain = retain_noop;
        bearer_port.release = release_noop;
        bearer_port.probe = Bearer::probe;
        bearer_port.create = Bearer::start;
        bearer_port.join = Bearer::start;
        bearer_port.resolve_endpoint = Bearer::resolve;
        bearer_port.release_bearer = unavailable_bearer_release;
        bearer_port.cancel = Bearer::cancel;
        bearer_port.prepare_credential = Bearer::prepare;
        bearer_port.release_credential = unavailable_secret_release;
        quic_port.struct_size = FLY_SESSION_QUIC_PORT_V2_SIZE;
        quic_port.abi_version = FLY_SESSION_ABI_VERSION_2;
        quic_port.context = &quic;
        quic_port.retain = retain_noop;
        quic_port.release = release_noop;
        quic_port.listen = Quic::start;
        quic_port.connect = Quic::start;
        quic_port.inspect_handshake = Quic::inspect;
        quic_port.exporter = Quic::exporter;
        quic_port.open_uni = unavailable_quic_stream;
        quic_port.open_bidi = Quic::open_bidi;
        quic_port.accept_uni = unavailable_quic_stream;
        quic_port.accept_bidi = Quic::accept_bidi;
        quic_port.write = Quic::write;
        quic_port.finish = unavailable_quic_control;
        quic_port.reset = unavailable_quic_control;
        quic_port.grant_read_credit = Quic::grant_read;
        quic_port.send_datagram = unavailable_quic_datagram;
        quic_port.payload_budget = unavailable_quic_query;
        quic_port.stats = unavailable_quic_query;
        quic_port.close = Quic::close;
        quic_port.cancel = Quic::cancel;
        ports.struct_size = FLY_SESSION_PORTS_V2_SIZE;
        ports.abi_version = FLY_SESSION_ABI_VERSION_2;
        ports.clock = &clock;
        ports.executor = &executor_port;
        ports.platform_state = &platform_port;
        ports.discovery = &discovery_port;
        if (secure_pairing_ports)
        {
            ports.key = &key_port;
            ports.crypto = &crypto_port;
            ports.tls_material = &tls_port;
            ports.secure_store = &secure_store_port;
            ports.bearer = &bearer_port;
            ports.quic = &quic_port;
            ports.object_store = &object_store_port;
        }
        if (enable_content)
        {
            content.item_count = content_items;
            content_port.struct_size = FLY_SESSION_CONTENT_PORT_V2_SIZE;
            content_port.abi_version = FLY_SESSION_ABI_VERSION_2;
            content_port.context = &content;
            content_port.retain = retain_noop;
            content_port.release = release_noop;
            content_port.query = Content::query;
            content_port.cancel = Content::cancel;
            ports.content = &content_port;
        }
        if (enable_dual_runtime)
        {
            dual_runtime_port.struct_size = FLY_SESSION_DUAL_RUNTIME_PORT_V2_SIZE;
            dual_runtime_port.abi_version = FLY_SESSION_ABI_VERSION_2;
            dual_runtime_port.context = &dual_runtime;
            dual_runtime_port.retain = retain_noop;
            dual_runtime_port.release = release_noop;
            dual_runtime_port.load = DualRuntime::load;
            dual_runtime_port.step = DualRuntime::step;
            dual_runtime_port.export_state = DualRuntime::export_state;
            dual_runtime_port.import_state = DualRuntime::import_state;
            dual_runtime_port.state_digest = DualRuntime::state_digest;
            ports.dual_runtime = &dual_runtime_port;
        }
        fly_session_config_v2 config{};
        config.struct_size = FLY_SESSION_CONFIG_V2_SIZE;
        config.abi_version = FLY_SESSION_ABI_VERSION_2;
        config.action_queue_capacity = 8;
        config.notice_queue_capacity = 8;
        check(fly_session_create_v2(&config, &ports, &engine) == FLY_SESSION_V2_OK,
              "public engine creates");
    }

public:
    ~EngineFixture()
    {
        if (engine)
        {
            fly_session_begin_shutdown_v2(engine, 900);
            executor.run_all();
            check(fly_session_destroy_v2(engine) == FLY_SESSION_V2_OK,
                  "public engine destroys after shutdown");
        }
    }

    fly_session_snapshot_v2 snapshot(std::vector<fly_session_action_descriptor_v2>* actions = nullptr)
    {
        fly_session_view_v2_t* view = nullptr;
        check(fly_session_acquire_view_v2(engine, &view) == FLY_SESSION_V2_OK,
              "view acquired");
        fly_session_snapshot_v2 value{};
        value.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE;
        value.abi_version = FLY_SESSION_ABI_VERSION_2;
        check(fly_session_view_read_v2(view, &value) == FLY_SESSION_V2_OK,
              "view read");
        if (actions)
        {
            actions->resize(value.action_count);
            std::uint32_t written = 0;
            check(fly_session_view_copy_actions_v2(
                      view, 0, actions->data(), value.action_count, &written) ==
                      FLY_SESSION_V2_OK && written == value.action_count,
                  "complete action page copied");
            for (auto& action : *actions)
                fly_session_approval_token_retain_v2(action.approval_token);
        }
        fly_session_view_release_v2(view);
        return value;
    }

    std::vector<fly_session_game_choice_v2> game_choices()
    {
        fly_session_view_v2_t* view = nullptr;
        check(fly_session_acquire_view_v2(engine, &view) == FLY_SESSION_V2_OK,
              "view acquired for game choices");
        fly_session_snapshot_v2 value{};
        value.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE;
        value.abi_version = FLY_SESSION_ABI_VERSION_2;
        check(fly_session_view_read_v2(view, &value) == FLY_SESSION_V2_OK,
              "view read for game choices");
        std::vector<fly_session_game_choice_v2> choices(value.game_choice_count);
        if (value.game_choice_count != 0)
        {
            std::uint32_t written = 0;
            check(fly_session_view_copy_game_choices_v2(
                      view, 0, choices.data(), value.game_choice_count,
                      &written) == FLY_SESSION_V2_OK &&
                      written == value.game_choice_count,
                  "complete game choice page copied");
        }
        fly_session_view_release_v2(view);
        return choices;
    }

    fly_session_pairing_v2 pairing()
    {
        fly_session_view_v2_t* view = nullptr;
        check(fly_session_acquire_view_v2(engine, &view) == FLY_SESSION_V2_OK,
              "pairing view acquired");
        fly_session_pairing_v2 value{};
        value.struct_size = FLY_SESSION_PAIRING_V2_SIZE;
        value.abi_version = FLY_SESSION_ABI_VERSION_2;
        check(fly_session_view_read_pairing_v2(view, &value) ==
                  FLY_SESSION_V2_OK,
              "pairing subview is available only after verified SAS derivation");
        fly_session_view_release_v2(view);
        return value;
    }
};

inline const fly_session_action_descriptor_v2* find_action(
    const std::vector<fly_session_action_descriptor_v2>& actions,
    std::uint32_t kind)
{
    for (const auto& action : actions)
        if (action.action_kind == kind) return &action;
    return nullptr;
}

inline constexpr char kLoopbackContentNameV1[] = "loopback";

inline std::array<std::uint8_t, 16> loopback_source_choice_ref_v1() noexcept
{
    std::array<std::uint8_t, 16> value{};
    value.fill(0xC1);
    return value;
}

inline std::array<std::uint8_t, 32> loopback_content_id_v1() noexcept
{
    std::array<std::uint8_t, 32> value{};
    value.fill(0xD1);
    return value;
}

inline std::vector<std::uint8_t> loopback_content_choice_record_v1()
{
    const auto name_size = static_cast<std::uint32_t>(
        sizeof(kLoopbackContentNameV1) - 1u);
    std::vector<std::uint8_t> record(
        FLY_SESSION_CONTENT_CHOICE_V2_HEADER_SIZE + name_size, 0);
    record[0] = 0;
    record[1] = 1;
    const auto choice = loopback_source_choice_ref_v1();
    const auto content = loopback_content_id_v1();
    std::copy(choice.begin(), choice.end(), record.begin() + 4);
    std::copy(content.begin(), content.end(), record.begin() + 20);
    record[52] = static_cast<std::uint8_t>(name_size >> 24u);
    record[53] = static_cast<std::uint8_t>(name_size >> 16u);
    record[54] = static_cast<std::uint8_t>(name_size >> 8u);
    record[55] = static_cast<std::uint8_t>(name_size);
    std::copy_n(reinterpret_cast<const std::uint8_t*>(kLoopbackContentNameV1),
                name_size, record.begin() + 56);
    return record;
}

inline std::array<std::uint8_t, 32> loopback_content_choice_hash_v1()
{
    const auto record = loopback_content_choice_record_v1();
    return flynes::session::wire::domain_hash(
        "flynes-content-choice-v1", record.data(), record.size());
}

inline std::vector<std::uint8_t> logical_v1(
    std::uint8_t type, const std::vector<std::uint8_t>& body)
{
    std::vector<std::uint8_t> value(8 + body.size() + 32, 0);
    value[0] = 1;
    value[1] = type;
    const auto size = static_cast<std::uint32_t>(body.size());
    value[4] = static_cast<std::uint8_t>(size >> 24u);
    value[5] = static_cast<std::uint8_t>(size >> 16u);
    value[6] = static_cast<std::uint8_t>(size >> 8u);
    value[7] = static_cast<std::uint8_t>(size);
    std::copy(body.begin(), body.end(), value.begin() + 8);
    const auto hash = flynes::session::wire::domain_hash(
        "flynes-gatt-logical-v1", value.data(), 8 + body.size());
    std::copy(hash.begin(), hash.end(), value.end() - 32);
    return value;
}

inline std::vector<std::uint8_t> pair_context_body()
{
    std::vector<std::uint8_t> value(80, 0);
    value[1] = 1;
    value[9] = 2;
    value[12] = 1;
    std::fill(value.begin() + 16, value.begin() + 32, std::uint8_t{0x11});
    std::fill(value.begin() + 32, value.begin() + 48, std::uint8_t{0x22});
    value[50] = 0xea;
    value[51] = 0x60;
    std::fill(value.begin() + 64, value.end(), std::uint8_t{0x33});
    return value;
}

inline std::array<std::uint8_t, 512> capability_summary()
{
    std::array<std::uint8_t, 512> value{};
    value[1] = 1;
    value[8] = 1;
    value[9] = 1;
    value[32] = 1;
    value[64] = 1;
    value[96] = 2;
    value[97] = 1;
    value[98] = 1;
    value[99] = 2;
    value[100] = 1;
    value[102] = 10;
    value[107] = 1;
    std::fill(value.begin() + 108, value.begin() + 140,
              std::uint8_t{1});
    return value;
}

inline void deliver_capability(EngineFixture& fixture)
{
    const auto bytes = capability_summary();
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), 0};
    check(fly_session_buffer_create_copy_v2(source, &buffer) ==
              FLY_SESSION_V2_OK,
          "bearer capability fixture owns immutable bytes");
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = fixture.bearer.last_token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = FLY_SESSION_PROVIDER_BEARER_CAPABILITIES_V2;
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = buffer;
    payload.logical_size = bytes.size();
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(fixture.bearer.inbox, &event) ==
              FLY_SESSION_V2_ACCEPTED,
          "typed bearer capability enters public engine");
    fly_session_buffer_release_v2(buffer);
}

inline std::array<std::uint8_t, 65> p256_generator()
{
    const char* hex =
        "046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
        "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5";
    const auto nibble = [](char ch) -> std::uint8_t {
        return static_cast<std::uint8_t>(
            ch >= '0' && ch <= '9' ? ch - '0' : ch - 'a' + 10);
    };
    std::array<std::uint8_t, 65> value{};
    for (std::size_t index = 0; index < value.size(); ++index)
        value[index] = static_cast<std::uint8_t>(
            (nibble(hex[index * 2]) << 4u) |
            nibble(hex[index * 2 + 1]));
    return value;
}

inline std::array<std::uint8_t, 65> p256_double_generator()
{
    const char* hex =
        "047cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978"
        "07775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1";
    const auto nibble = [](char ch) -> std::uint8_t {
        return static_cast<std::uint8_t>(
            ch >= '0' && ch <= '9' ? ch - '0' : ch - 'a' + 10);
    };
    std::array<std::uint8_t, 65> value{};
    for (std::size_t index = 0; index < value.size(); ++index)
        value[index] = static_cast<std::uint8_t>(
            (nibble(hex[index * 2]) << 4u) |
            nibble(hex[index * 2 + 1]));
    return value;
}

inline void deliver_provider_buffer(
    fly_session_inbox_v2_t* inbox, const fly_session_op_token_v2& token,
    std::uint32_t kind, const std::uint8_t* bytes, std::size_t size)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{
        bytes, static_cast<std::uint32_t>(size), 0};
    check(fly_session_buffer_create_copy_v2(source, &buffer) ==
              FLY_SESSION_V2_OK,
          "provider result fixture owns immutable bytes");
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = buffer;
    payload.logical_size = size;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    const auto delivered = fly_session_deliver_v2(inbox, &event);
    if (delivered != FLY_SESSION_V2_ACCEPTED)
    {
        char message[256];
        std::snprintf(message, sizeof(message),
                      "typed provider buffer completion enters public engine "
                      "(kind 0x%04x, %zu bytes, terminal 1, result %d)",
                      static_cast<unsigned>(kind), size,
                      static_cast<int>(delivered));
        check(false, message);
    }
    fly_session_buffer_release_v2(buffer);
}

inline void deliver_provider_end(
    fly_session_inbox_v2_t* inbox, const fly_session_op_token_v2& token,
    std::uint32_t kind, fly_session_result_v2 result = FLY_SESSION_V2_OK)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = result;
    event.payload_kind = kind;
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    const auto end_result = fly_session_deliver_v2(inbox, &event);
    check(end_result == FLY_SESSION_V2_ACCEPTED,
          "typed provider end completion enters public engine");
}

// Builds one provider hash completion without asserting the engine result, so
// negative cases can observe the exact public rejection code.
inline fly_session_result_v2 deliver_provider_hash_raw(
    fly_session_inbox_v2_t* inbox, const fly_session_op_token_v2& token,
    std::uint32_t kind, fly_session_resource_handle_v2 resource,
    const std::array<std::uint8_t, 32>& hash, std::uint32_t event_sequence = 1)
{
    static constexpr std::array<std::uint8_t, 1> reference{{1}};
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{reference.data(), 1, 0};
    check(fly_session_buffer_create_copy_v2(source, &buffer) ==
              FLY_SESSION_V2_OK,
          "provider handle fixture owns durable reference bytes");
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = event_sequence;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_hash_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_HASH_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    payload.buffer = buffer;
    std::copy(hash.begin(), hash.end(), payload.hash);
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    const auto result = fly_session_deliver_v2(inbox, &event);
    fly_session_buffer_release_v2(buffer);
    return result;
}

inline void deliver_provider_hash(
    fly_session_inbox_v2_t* inbox, const fly_session_op_token_v2& token,
    std::uint32_t kind, fly_session_resource_handle_v2 resource,
    const std::array<std::uint8_t, 32>& hash)
{
    check(deliver_provider_hash_raw(inbox, token, kind, resource, hash) ==
              FLY_SESSION_V2_ACCEPTED,
          "typed provider hash completion enters public engine");
}

inline void deliver_provider_hash_buffer(
    fly_session_inbox_v2_t* inbox, const fly_session_op_token_v2& token,
    std::uint32_t kind, fly_session_resource_handle_v2 resource,
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& hash)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{
        bytes, static_cast<std::uint32_t>(size), 0};
    check(fly_session_buffer_create_copy_v2(source, &buffer) ==
              FLY_SESSION_V2_OK,
          "provider hash fixture owns exact immutable bytes");
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_hash_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_HASH_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    payload.buffer = buffer;
    std::copy(hash.begin(), hash.end(), payload.hash);
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(inbox, &event) == FLY_SESSION_V2_ACCEPTED,
          "typed provider hash+buffer completion enters public engine");
    fly_session_buffer_release_v2(buffer);
}

inline void deliver_provider_resource(
    fly_session_inbox_v2_t* inbox, const fly_session_op_token_v2& token,
    std::uint32_t kind, fly_session_resource_handle_v2 resource)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    payload.generation = token.connection_generation;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(inbox, &event) == FLY_SESSION_V2_ACCEPTED,
          "typed provider resource completion enters public engine");
}

inline void deliver_provider_resource_pair(
    fly_session_inbox_v2_t* inbox, const fly_session_op_token_v2& token,
    std::uint32_t kind, fly_session_resource_handle_v2 resource,
    std::uint64_t value0)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    payload.generation = token.connection_generation;
    payload.value0 = value0;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(inbox, &event) == FLY_SESSION_V2_ACCEPTED,
          "typed paired-resource completion enters public engine");
}

inline void deliver_provider_stream_data(
    fly_session_inbox_v2_t* inbox, const fly_session_op_token_v2& token,
    const std::uint8_t* bytes, std::size_t size)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{
        size == 0 ? nullptr : bytes, static_cast<std::uint32_t>(size), 0};
    check(fly_session_buffer_create_copy_v2(source, &buffer) ==
              FLY_SESSION_V2_OK,
          "stream fixture owns immutable bytes");
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 0;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = FLY_SESSION_PROVIDER_QUIC_DATA_V2;
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = buffer;
    payload.logical_size = size;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(inbox, &event) == FLY_SESSION_V2_ACCEPTED,
          "typed QUIC stream bytes enter public engine");
    fly_session_buffer_release_v2(buffer);
}

inline void deliver_discovery_end(EngineFixture& fixture);

inline void consume_physical_acks(EngineFixture& fixture, std::size_t)
{
    fixture.executor.run_all();
}

inline void deliver_logical(EngineFixture& fixture,
                     const std::vector<std::uint8_t>& logical,
                     std::uint8_t type, std::uint16_t message_id,
                     std::uint64_t first_sequence)
{
    const std::size_t ack_begin = fixture.discovery.written_fragments.size();
    std::vector<std::vector<std::uint8_t>> fragments;
    check(flynes::session::wire::fragment_gatt_logical(
              logical.data(), logical.size(), type, message_id, 20,
              &fragments) ==
              flynes::session::wire::GattFragmentResult::Accepted,
          "test logical message fragments at ATT fallback");
    auto sequence = first_sequence;
    for (const auto& fragment : fragments)
    {
        fly_session_buffer_v2_t* buffer = nullptr;
        const fly_session_bytes_v2 source{
            fragment.data(), static_cast<std::uint32_t>(fragment.size()), 0};
        check(fly_session_buffer_create_copy_v2(source, &buffer) ==
                  FLY_SESSION_V2_OK,
              "test logical fragment owns an immutable buffer");
        fly_session_port_event_v2 event{};
        event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
        event.abi_version = FLY_SESSION_ABI_VERSION_2;
        /* A class-2 byte event is accepted only under the engine's own subscription
         * token; `last_token` has been overwritten by later operations. */
        event.token = fixture.discovery.subscription_token;
        event.event_sequence = sequence++;
        event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
        event.result = FLY_SESSION_V2_OK;
        event.payload_kind = FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2;
        fly_session_provider_buffer_event_v2 payload{};
        payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
        payload.abi_version = FLY_SESSION_ABI_VERSION_2;
        payload.buffer = buffer;
        payload.logical_size = fragment.size();
        payload.generation = event.token.connection_generation;
        event.payload_size = sizeof(payload);
        std::memcpy(event.payload, &payload, sizeof(payload));
        auto result = fly_session_deliver_v2(fixture.discovery.inbox, &event);
        if (result == FLY_SESSION_V2_BACKPRESSURE)
        {
            fixture.executor.run_all();
            result = fly_session_deliver_v2(fixture.discovery.inbox, &event);
        }
        check(result == FLY_SESSION_V2_ACCEPTED,
              "test logical fragment enters the public inbox");
        fly_session_buffer_release_v2(buffer);
    }
    // The public engine now implements the frozen type-16 physical ACK. Keep
    // the older semantic-flow assertions focused on their business message by
    // driving and removing only the ACK fragments produced for this delivery.
    consume_physical_acks(fixture, ack_begin);
}

inline void deliver_discovery_end(EngineFixture& fixture)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = fixture.discovery.write_token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = FLY_SESSION_PROVIDER_DISCOVERY_END_V2;
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    const auto delivered = fly_session_deliver_v2(
        fixture.discovery.inbox, &event);
    check(delivered == FLY_SESSION_V2_ACCEPTED,
          "outgoing GATT fragment completion enters the public inbox");
}

inline void submit(EngineFixture& fixture,
            const fly_session_action_descriptor_v2& descriptor,
            std::uint64_t request_id, bool join)
{
    fly_session_action_v2 action{};
    action.struct_size = FLY_SESSION_ACTION_V2_SIZE;
    action.abi_version = FLY_SESSION_ABI_VERSION_2;
    action.request_id = request_id;
    action.expected_view_revision = fixture.snapshot().view_revision;
    action.approval_token = descriptor.approval_token;
    if (join)
    {
        action.choice_size = FLY_SESSION_ACTION_CHOICE_V2_SIZE;
        action.choice.struct_size = FLY_SESSION_ACTION_CHOICE_V2_SIZE;
        action.choice.abi_version = FLY_SESSION_ABI_VERSION_2;
        action.choice.choice_kind = FLY_SESSION_CHOICE_INVITE_CODE_V2;
        const std::array<std::uint8_t, 6> code{{'0', '1', '2', '3', '4', '5'}};
        std::copy(code.begin(), code.end(), action.choice.choice_id);
    }
    check(fly_session_submit_action_v2(fixture.engine, &action) ==
              FLY_SESSION_V2_ACCEPTED,
          "public link action accepted for worker validation");
    fixture.executor.run_all();
}

inline void submit_reference(EngineFixture& fixture,
                      const fly_session_action_descriptor_v2& descriptor,
                      std::uint64_t request_id, std::uint64_t value)
{
    fly_session_action_v2 action{};
    action.struct_size = FLY_SESSION_ACTION_V2_SIZE;
    action.abi_version = FLY_SESSION_ABI_VERSION_2;
    action.request_id = request_id;
    action.expected_view_revision = fixture.snapshot().view_revision;
    action.approval_token = descriptor.approval_token;
    action.choice_size = FLY_SESSION_ACTION_CHOICE_V2_SIZE;
    action.choice.struct_size = FLY_SESSION_ACTION_CHOICE_V2_SIZE;
    action.choice.abi_version = FLY_SESSION_ABI_VERSION_2;
    action.choice.choice_kind = FLY_SESSION_CHOICE_REFERENCE_V2;
    action.choice.value = value;
    check(fly_session_submit_action_v2(fixture.engine, &action) ==
              FLY_SESSION_V2_ACCEPTED,
          "public reference action accepted for worker validation");
    fixture.executor.run_all();
}

inline void submit_choice(EngineFixture& fixture,
                          const fly_session_action_descriptor_v2& descriptor,
                          std::uint64_t request_id,
                          const std::uint8_t choice_id[16])
{
    fly_session_action_v2 action{};
    action.struct_size = FLY_SESSION_ACTION_V2_SIZE;
    action.abi_version = FLY_SESSION_ABI_VERSION_2;
    action.request_id = request_id;
    action.expected_view_revision = fixture.snapshot().view_revision;
    action.approval_token = descriptor.approval_token;
    action.choice_size = FLY_SESSION_ACTION_CHOICE_V2_SIZE;
    action.choice.struct_size = FLY_SESSION_ACTION_CHOICE_V2_SIZE;
    action.choice.abi_version = FLY_SESSION_ABI_VERSION_2;
    action.choice.choice_kind = FLY_SESSION_CHOICE_REFERENCE_V2;
    if (choice_id)
        std::memcpy(action.choice.choice_id, choice_id, 16u);
    check(fly_session_submit_action_v2(fixture.engine, &action) ==
              FLY_SESSION_V2_ACCEPTED,
          "public SELECT_CONTENT action accepted for worker validation");
    fixture.executor.run_all();
}

inline void start_responder_probe(EngineFixture& fixture, std::uint64_t request_id)
{
    fixture.platform.ready();
    fixture.executor.run_all();
    std::vector<fly_session_action_descriptor_v2> actions;
    fixture.snapshot(&actions);
    const auto* join = find_action(actions, FLY_SESSION_ACTION_JOIN_CODE_V2);
    check(join != nullptr, "responder fixture exposes join code");
    if (join)
        submit(fixture, *join, request_id, true);

    fly_session_port_event_v2 connection{};
    connection.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    connection.abi_version = FLY_SESSION_ABI_VERSION_2;
    connection.token = fixture.discovery.last_token;
    connection.event_sequence = 1;
    connection.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    connection.terminal = 1;
    connection.result = FLY_SESSION_V2_OK;
    connection.payload_kind = FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2;
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = 141;
    payload.generation = connection.token.connection_generation;
    payload.value0 = FLY_SESSION_DISCOVERY_PHYSICAL_CENTRAL_V2;
    payload.value1 = 23;
    connection.payload_size = sizeof(payload);
    std::memcpy(connection.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(fixture.discovery.inbox, &connection) ==
              FLY_SESSION_V2_ACCEPTED,
          "responder fixture accepts discovery connection");
    fixture.executor.run_all();

    const auto pair_context = logical_v1(
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::PairContext),
        pair_context_body());
    deliver_logical(
        fixture, pair_context,
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::PairContext),
        31, 1000);
    fixture.executor.run_all();
    for (auto& action : actions)
        fly_session_approval_token_release_v2(action.approval_token);
}

inline void deliver_cancelled_provider_terminal(
    fly_session_inbox_v2_t* inbox, const fly_session_op_token_v2& token,
    std::uint32_t payload_kind)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_CANCELLED;
    event.payload_kind = payload_kind;
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(inbox, &event) == FLY_SESSION_V2_ACCEPTED,
          "cancelled provider terminal enters the shutting-down engine");
}


/*
 * ------------------------------------------------------------------------- *
 * The loopback transport: increment 1 of the two-engine driver.
 *
 * Provider events come in two kinds, and this class exists to keep them apart:
 *
 *   class 1 — OPERATION TERMINALS: the result of an effect the engine itself
 *     issued. They carry that effect's token and terminal = 1. Answering them is
 *     local provider work (the counted pump in the next increment).
 *
 *   class 2 — EXTERNALLY ORIGINATED EVENTS: not the result of any local effect but
 *     a fact about the transport or the peer: DISCOVERY_CONNECTION (the peer is
 *     connected), DISCOVERY_BYTES (the peer wrote a GATT fragment), QUIC_DATA (the
 *     peer wrote stream bytes), PLATFORM_STATE.
 *
 * Class 2 is produced ONLY here. A test must never author one: doing so is exactly
 * the fake-transport shortcut the MVP-LOBBY acceptance line forbids, and it is what
 * makes an exchange stop being a real one. The single-engine regression test does
 * author its own DISCOVERY_CONNECTION (resources 161 / 41); that is acceptable
 * there precisely because it makes no two-engine claim, and it is what this class
 * replaces for the real one.
 *
 * Topology lives here too, not in the test: which end advertises, which scans, and
 * that both ends are on the SAME link.
 * ------------------------------------------------------------------------- */

enum class LoopbackRole : std::uint8_t
{
    /* Advertises and serves the connection; the provider reports it as the
     * peripheral end. */
    AdvertiserPeripheral = 1,
    /* Scans, connects and subscribes; reported as the central end. */
    ScannerCentral = 2
};

class LoopbackTransport final
{
public:
    void attach(LoopbackRole role, EngineFixture& fixture) noexcept
    {
        if (role == LoopbackRole::AdvertiserPeripheral)
            peripheral_ = &fixture;
        else
            central_ = &fixture;
        /* The engine's QUIC port is answered from the link's own facts, so the
         * link must be reachable from the fixture the pump is driving. */
        fixture.attached_link = this;
    }

    /*
     * Hands the provider-owned connection to each attached end that has started its
     * discovery role. Both ends receive the SAME link handle, which is what makes
     * them two ends of one link rather than two unrelated connections. Idempotent:
     * an end is connected at most once.
     */
    int connect_ends()
    {
        int delivered = 0;
        if (peripheral_ != nullptr && !peripheral_connected_ &&
            peripheral_->discovery.advertisements == 1)
        {
            deliver_connection(*peripheral_,
                               FLY_SESSION_DISCOVERY_PHYSICAL_PERIPHERAL_V2);
            peripheral_connected_ = true;
            ++delivered;
        }
        if (central_ != nullptr && !central_connected_ &&
            central_->discovery.scans == 1)
        {
            deliver_connection(*central_,
                               FLY_SESSION_DISCOVERY_PHYSICAL_CENTRAL_V2);
            central_connected_ = true;
            ++delivered;
        }
        connections_ += delivered;
        return delivered;
    }

    [[nodiscard]] fly_session_resource_handle_v2 link_resource() const noexcept
    {
        return link_resource_;
    }
    [[nodiscard]] int connections() const noexcept { return connections_; }
    /*
     * Topology belongs to the transport, not to the test: the relay asks the
     * transport which end is which instead of trusting a caller's argument order.
     */
    [[nodiscard]] EngineFixture* peripheral_fixture() const noexcept
    {
        return peripheral_;
    }
    [[nodiscard]] EngineFixture* central_fixture() const noexcept
    {
        return central_;
    }
    [[nodiscard]] bool peripheral_connected() const noexcept
    {
        return peripheral_connected_;
    }
    [[nodiscard]] bool central_connected() const noexcept
    {
        return central_connected_;
    }

    /*
     * --------------------------------------------------------------------- *
     * Step 5: the LINK's own QUIC facts.
     *
     * One link is one TLS session, so the values that must agree across the two
     * ends are the link's, not a test's and not either engine's:
     *
     *   the exporter   - one 32-byte value for BOTH ends. The channel id each
     *                    engine derives depends on it
     *                    (InitialQuicBindScheduler::derive and the engine's own
     *                    channel-id derivation), so two different values would
     *                    make the ChannelBind proofs unverifiable. It is derived
     *                    from the link's own material (an HMAC over a fixed link
     *                    label keyed by the link handle), never from a peer
     *                    claim.
     *
     *   the handshake  - the link's TLS facts, encoded by the repository's own
     *                    wire::encode_quic_handshake_facts_v2 and hashed with
     *                    the repository's own sha256. The connector's and the
     *                    listener's facts are NOT byte-identical, and the engine
     *                    requires that: verify_quic_handshake_v2
     *                    (quic_contract.cpp:131) demands the connector report
     *                    pin_verifier_invoked and peer_certificate_verified true
     *                    with the SPKI hash equal to the policy pin, while
     *                    verify_quic_listener_handshake_v2 (:148) demands the
     *                    listener report both false with an all-zero hash. Every
     *                    LINK-wide fact - TLS 1.3, full handshake, no resumption,
     *                    no 0-RTT, the same ALPN - is shared and comes from here.
     * --------------------------------------------------------------------- */

    /*
     * The Nth stream either role registers IS the same physical stream, so the
     * two ends are paired by their own registration order, which the protocol
     * fixes: the bind stream first, then the Control stream.
     */
    struct QuicStreamAnswer final
    {
        fly_session_resource_handle_v2 send = 0;
        fly_session_resource_handle_v2 receive = 0;
    };

    /*
     * Both handles are the SAME link-wide stream handle, and that is deliberate.
     * The two schedulers that own the two streams use the two handles the answer
     * carries differently: InitialQuicBindScheduler reads on `value0`
     * (initial_quic_bind_scheduler.cpp:447-448:
     * `resources_.send_stream = parsed.resource; resources_.receive_stream =
     * parsed.value0;`) while LinkHandshakeScheduler takes `resource` as its one
     * Control stream and uses that single handle for BOTH the write and the read
     * (link_handshake_scheduler.cpp:1129 `control_stream_ =
     * completion.payload.resource;`, then :355 `effect.resource =
     * control_stream_;` for writes and :486 `effect.resource = control_stream_;`
     * for reads). Handing the same handle back in `resource` and `value0` is the
     * only assignment that satisfies both conventions on one physical stream, and
     * it is also what QUIC itself does: a bidirectional stream has one id at
     * both ends.
     */
    QuicStreamAnswer register_quic_stream(bool listener)
    {
        const std::size_t index =
            listener ? listener_streams_++ : connector_streams_++;
        while (streams_.size() <= index) streams_.push_back(QuicStream{});
        QuicStream& stream = streams_[index];
        if (stream.handle == 0) stream.handle = next_quic_handle_++;
        if (listener)
        {
            check(!stream.listener_registered,
                  "the link's QUIC listener accepts each stream position once");
            stream.listener_registered = true;
        }
        else
        {
            check(!stream.connector_registered,
                  "the link's QUIC connector opens each stream position once");
            stream.connector_registered = true;
        }
        return QuicStreamAnswer{stream.handle, stream.handle};
    }

    /*
     * Records which end of the link is the QUIC listener, from the engine's own
     * `listen` request (Quic::start: a non-zero TLS material argument is a
     * listen), and pins the listener's own SPKI hash as the link's pin.
     */
    void note_quic_listener(EngineFixture& fixture)
    {
        ensure_link_facts();
        if (quic_listener_ == nullptr) quic_listener_ = &fixture;
        check(quic_listener_ == &fixture,
              "one link has exactly one QUIC listener");
        const auto& spki = fixture.tls.last_spki_hash;
        const bool nonzero = std::any_of(spki.begin(), spki.end(),
                                         [](std::uint8_t value) {
                                             return value != 0;
                                         });
        check(nonzero,
              "the link knows the listener's real TLS SPKI hash before it reports "
              "a handshake for either end");
        pin_ = spki;
    }

    [[nodiscard]] EngineFixture* quic_listener() const noexcept
    {
        return quic_listener_;
    }

    /* Provider-owned QUIC objects (the connection, and the streams registered
     * below) are allocated by the LINK, so a handle can never collide with one a
     * test or the fixture invented. */
    fly_session_resource_handle_v2 allocate_quic_handle() noexcept
    {
        return next_quic_handle_++;
    }
    [[nodiscard]] const std::array<std::uint8_t, 32>& exporter()
    {
        ensure_link_facts();
        return exporter_;
    }
    [[nodiscard]] const std::array<std::uint8_t, 32>& pin() const noexcept
    {
        return pin_;
    }

    /*
     * The encoded handshake facts for one end of the link. `listener` selects
     * the role the engine's own verifier requires; a connector's facts carry the
     * pin THIS engine asked for (captured from its own policy), and the transport
     * refuses to report a pin the listener's own TLS material does not hash to.
     */
    bool quic_handshake_facts(EngineFixture& fixture, bool listener,
                              std::vector<std::uint8_t>* out)
    {
        ensure_link_facts();
        if (out == nullptr) return false;
        wire::QuicHandshakeFactsV2 facts{};
        facts.tls_major = 1;
        facts.tls_minor = 3;
        facts.full_handshake = true;
        static constexpr char kAlpn[] = "flynes-nearby/2";
        std::copy_n(kAlpn, sizeof(kAlpn) - 1, facts.alpn.begin());
        if (!listener)
        {
            /* wire::verify_quic_handshake_v2 requires both to be true and the
             * hash to equal the policy pin. */
            facts.pin_verifier_invoked = true;
            facts.peer_certificate_verified = true;
            facts.der_spki_hash = fixture.quic.last_policy_spki;
            if (has_pin_override_)
            {
                /* W3 tamper matrix: the transport presents a certificate whose
                 * SPKI the connector did NOT pin, while still reporting that it
                 * verified it. Both booleans stay true, so the engine's own
                 * comparison of the reported hash against its connect policy is
                 * the only gate left - which is exactly the case this exercises. */
                facts.der_spki_hash = pin_override_;
            }
            else
            {
                check(facts.der_spki_hash == pin_,
                      "the connector's own QUIC pin is the listener's real SPKI "
                      "hash, so the facts the link reports are not a fabricated "
                      "peer claim");
            }
        }
        std::array<std::uint8_t, wire::kQuicHandshakeFactsWireSizeV2> encoded{};
        if (wire::encode_quic_handshake_facts_v2(facts, &encoded) !=
            wire::Status::Ok)
            return false;
        out->assign(encoded.begin(), encoded.end());
        return true;
    }

    /*
     * A TRANSPORT-side filter for the single-sided-READY negative case. While it
     * is armed for a direction, the transport does not deliver that direction's
     * LINK_READY/ACK frames; it does not alter, reorder or fabricate a byte. The
     * withheld unit stays at the head of that direction's queue and is delivered
     * verbatim by the next round after `release_withheld()`, so the same bytes
     * that were withheld are the bytes that finally cross.
     */
    enum class QuicFilter : std::uint8_t
    {
        None = 0,
        PeripheralToCentral = 1,
        CentralToPeripheral = 2
    };

    void withhold_link_ready_in(QuicFilter direction) noexcept
    {
        withheld_ = direction;
    }
    void release_withheld() noexcept { withheld_ = QuicFilter::None; }
    [[nodiscard]] QuicFilter withheld_direction() const noexcept
    {
        return withheld_;
    }

    /*
     * --------------------------------------------------------------------- *
     * W3 tamper matrix: the transport's two explicit, narrowly-scoped faults.
     *
     * (1) `present_unpinned_certificate` makes the TLS facts this link reports
     *     carry an SPKI the connector never pinned, while still claiming it
     *     verified the peer. It is the man-in-the-middle case, and only the
     *     engine's own policy comparison can catch it.
     *
     * (2) `tamper_with` XORs exactly ONE byte of exactly ONE unit, named by
     *     (direction, selector, 1-based occurrence) and latched off after the
     *     single injection. It cannot reorder, drop, duplicate or fabricate a
     *     unit, it never runs unless a test armed it, and it changes the
     *     SENDER'S OWN encoded bytes - i.e. it models an on-path attacker, not a
     *     second protocol implementation. It is deliberately not a general
     *     escape hatch: the selector vocabulary is closed (an app frame of a
     *     given object kind, or a ChannelBind record on the bind stream).
     * --------------------------------------------------------------------- */
    void present_unpinned_certificate(std::array<std::uint8_t, 32> spki) noexcept
    {
        pin_override_ = spki;
        has_pin_override_ = true;
    }
    void clear_pin_override() noexcept { has_pin_override_ = false; }
    [[nodiscard]] bool pin_overridden() const noexcept
    {
        return has_pin_override_;
    }

    enum class TamperSelector : std::uint8_t
    {
        /* The Nth unit in this direction whose app-frame tag (bytes 4..5) equals
         * `object_kind`. */
        AppFrameOfTag = 0,
        /* The Nth unit in this direction on the QUIC bind stream. Those records
         * are the ChannelBind proof/ACK, which are NOT app frames
         * (`[preamble] || u32be(len) || object`), so they cannot be named by a
         * tag. */
        BindStreamRecord = 1
    };

    struct TamperRule final
    {
        QuicFilter direction = QuicFilter::None;
        TamperSelector selector = TamperSelector::AppFrameOfTag;
        std::uint16_t object_kind = 0;
        std::uint32_t occurrence = 1;
        std::uint32_t byte_index = 0;
        std::uint8_t xor_mask = 0x01;
    };

    void tamper_with(TamperRule rule) noexcept
    {
        tamper_ = rule;
        tamper_armed_ = true;
        tamper_occurrences_ = 0;
    }
    void clear_tamper() noexcept { tamper_armed_ = false; }
    [[nodiscard]] bool tamper_armed() const noexcept { return tamper_armed_; }
    [[nodiscard]] std::uint32_t tampered_units() const noexcept
    {
        return tampered_units_;
    }
    [[nodiscard]] std::uint32_t tampered_index() const noexcept
    {
        return tampered_index_;
    }
    [[nodiscard]] std::uint8_t tampered_before() const noexcept
    {
        return tampered_before_;
    }
    [[nodiscard]] std::uint8_t tampered_after() const noexcept
    {
        return tampered_after_;
    }

    /* The bind stream is stream position 0 on this link, and both ends are told
     * the same handle for it, so this is the exact identity of the bind stream. */
    [[nodiscard]] fly_session_resource_handle_v2 bind_stream_handle() const noexcept
    {
        return streams_.empty() ? 0 : streams_[0].handle;
    }

    /*
     * Called once per STAGED unit - i.e. once per write the sender issued, in
     * order - and reports whether the armed rule targets this unit. Pure
     * bookkeeping: nothing is rewritten here.
     */
    [[nodiscard]] bool targets_unit(QuicFilter direction,
                                    fly_session_resource_handle_v2 stream,
                                    const std::vector<std::uint8_t>& bytes)
    {
        if (!tamper_armed_ || tamper_.direction != direction) return false;
        bool matches = false;
        if (tamper_.selector == TamperSelector::BindStreamRecord)
            matches = stream != 0 && stream == bind_stream_handle();
        else
        {
            if (bytes.size() < 6u) return false;
            const auto tag = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(bytes[4]) << 8u) |
                static_cast<std::uint16_t>(bytes[5]));
            matches = tag == tamper_.object_kind;
        }
        if (!matches) return false;
        ++tamper_occurrences_;
        return tamper_occurrences_ == tamper_.occurrence;
    }

    /*
     * Applies the armed rule to `bytes` in place and latches it off, so the
     * fault is injected exactly once. A rule naming a byte the unit does not have
     * is a test error and is REPORTED, never silently skipped: a tamper that
     * never happened would make a negative case pass for the wrong reason.
     */
    bool apply_tamper(std::vector<std::uint8_t>* bytes)
    {
        if (bytes == nullptr || tamper_.byte_index >= bytes->size())
        {
            check(false,
                  "the armed tamper names a byte the sender's own unit really has");
            return false;
        }
        tampered_index_ = tamper_.byte_index;
        tampered_before_ = (*bytes)[tamper_.byte_index];
        (*bytes)[tamper_.byte_index] = static_cast<std::uint8_t>(
            tampered_before_ ^ tamper_.xor_mask);
        tampered_after_ = (*bytes)[tamper_.byte_index];
        ++tampered_units_;
        tamper_armed_ = false;
        return true;
    }

    /*
     * (3) `tamper_logical_with` injects at the OTHER wire boundary: the pre-QUIC
     *     GATT relay, which carries the pair records (commit, reveal, signature,
     *     key-confirm, credential) before any QUIC connection exists. It rewrites
     *     exactly ONE byte of the BODY of exactly ONE logical message, named by
     *     (direction, logical type, 1-based occurrence), and then recomputes that
     *     record's own trailing integrity hash so the record stays internally
     *     consistent.
     *
     *     Recomputing the hash is deliberate, not a convenience: the trailing
     *     hash is an UNKEYED domain hash, so an on-path attacker recomputes it.
     *     Leaving it stale would make the transport layer eat the tamper for free
     *     and the negative case would prove nothing about the pair layer, which
     *     is the layer under test. Nothing is reframed: the fragment count, order
     *     and sizes are unchanged, so the sender's own stream shape is preserved.
     */
    enum class GattTamperSelector : std::uint8_t
    {
        /* The Nth logical message in this direction whose logical type equals
         * `logical_type` (see wire::GattLogicalType). */
        LogicalType = 0
    };

    static constexpr std::uint32_t kLastBodyByte = 0xFFFFFFFFu;

    struct GattTamperRule final
    {
        /* `None` means "either direction", i.e. every logical message of the
         * named type on this link. A pair record is written by BOTH ends and may
         * be retransmitted, and an on-path attacker does not get to pick which
         * copy arrives first, so the strong form of the case is to tamper every
         * copy rather than only one end's first attempt. */
        QuicFilter direction = QuicFilter::None;
        GattTamperSelector selector = GattTamperSelector::LogicalType;
        std::uint8_t logical_type = 0;
        /* 1-based index among the matching messages, or 0 for ALL of them. */
        std::uint32_t occurrence = 1;
        /* Offset inside the logical message BODY. `kLastBodyByte` names the last
         * body byte, which is where the trailing cryptographic material lives
         * (a 64-byte signature, a 16-byte AEAD tag). */
        std::uint32_t body_index = 0;
        std::uint8_t xor_mask = 0x01;
    };

    void tamper_logical_with(GattTamperRule rule) noexcept
    {
        gatt_tamper_ = rule;
        gatt_tamper_armed_ = true;
        gatt_tamper_occurrences_ = 0;
    }
    void clear_logical_tamper() noexcept { gatt_tamper_armed_ = false; }
    [[nodiscard]] bool logical_tamper_armed() const noexcept
    {
        return gatt_tamper_armed_;
    }
    [[nodiscard]] std::uint32_t tampered_logical_messages() const noexcept
    {
        return tampered_logical_;
    }
    /* Times the tamper hook could not reassemble a logical message it was asked
     * about. Non-zero means the GATT tamper cases for that run prove nothing. */
    [[nodiscard]] std::uint32_t logical_tamper_reassembly_mismatches() const noexcept
    {
        return gatt_reassembly_mismatches_;
    }

    /*
     * How many TAMPERED logical messages were actually accepted by the receiving
     * engine's byte-event intake. This is the counter that separates "the harness
     * rewrote a byte of a record that then crossed" from "the harness rewrote a
     * byte that never left", and without it a negative case that still reaches
     * the lobby cannot be attributed to the engine at all.
     */
    void note_tampered_group_delivered() noexcept
    {
        ++tampered_groups_delivered_;
    }
    [[nodiscard]] std::uint32_t tampered_logical_delivered() const noexcept
    {
        return tampered_groups_delivered_;
    }
    [[nodiscard]] std::uint32_t logical_tamper_body_index() const noexcept
    {
        return gatt_tamper_index_;
    }
    [[nodiscard]] std::uint8_t logical_tamper_before() const noexcept
    {
        return gatt_tamper_before_;
    }

    /*
     * Called by the relay with the fragment group that makes up ONE logical
     * message, in order. Returns true when the armed rule rewrote a byte of it.
     * The group is edited in place.
     */
    bool tamper_logical_group(QuicFilter direction,
                              std::vector<std::vector<std::uint8_t>>* group)
    {
        if (!gatt_tamper_armed_ || group == nullptr || group->empty()) return false;
        if (gatt_tamper_.direction != QuicFilter::None &&
            gatt_tamper_.direction != direction)
            return false;
        const std::size_t header = wire::kGattPhysicalHeaderSize;
        std::vector<std::uint8_t> logical;
        for (const auto& fragment : *group)
        {
            if (fragment.size() <= header) return false;
            logical.insert(logical.end(),
                           fragment.begin() + static_cast<std::ptrdiff_t>(header),
                           fragment.end());
        }
        if (logical.size() < wire::kGattLogicalMinSize) return false;
        if (logical[1] != gatt_tamper_.logical_type) return false;
        ++gatt_tamper_occurrences_;
        if (gatt_tamper_.occurrence != 0u &&
            gatt_tamper_occurrences_ != gatt_tamper_.occurrence)
            return false;
        const std::size_t body_size =
            (static_cast<std::size_t>(logical[4]) << 24u) |
            (static_cast<std::size_t>(logical[5]) << 16u) |
            (static_cast<std::size_t>(logical[6]) << 8u) |
            static_cast<std::size_t>(logical[7]);
        if (body_size == 0u || logical.size() < 8u + body_size + 32u)
        {
            /* The group the relay hands over starts at its own delivery
             * watermark, which a BACKPRESSURE return can leave in the MIDDLE of a
             * logical message; such a group is not this record's boundary and is
             * not a harness error. It is COUNTED so a run where it happened is
             * visible, and `logical_tamper_reassembly_mismatches()` lets a caller
             * refuse to draw conclusions from one.
             */
            ++gatt_reassembly_mismatches_;
            return false;
        }
        /* The record is the PREFIX of the reassembled bytes: anything past it
         * belongs to the next logical message and must cross untouched. */
        const std::size_t record_size = 8u + body_size + 32u;
        std::size_t index = gatt_tamper_.body_index;
        if (index == kLastBodyByte) index = body_size - 1u;
        if (index >= body_size)
        {
            check(false,
                  "the armed logical-message tamper names a byte the sender's own "
                  "record really has");
            return false;
        }
        const std::size_t absolute = 8u + index;
        gatt_tamper_index_ = static_cast<std::uint32_t>(index);
        gatt_tamper_before_ = logical[absolute];
        logical[absolute] = static_cast<std::uint8_t>(
            gatt_tamper_before_ ^ gatt_tamper_.xor_mask);
        /* Recompute THIS record's own trailing integrity hash. */
        const char* domain =
            logical[0] == 2u ? "flynes-gatt-logical-v2" : "flynes-gatt-logical-v1";
        const auto digest =
            wire::domain_hash(domain, logical.data(), 8u + body_size);
        std::copy(digest.begin(), digest.end(),
                  logical.begin() + static_cast<std::ptrdiff_t>(8u + body_size));
        /* Write the record back into the same fragment payloads, only as far as
         * the record goes. */
        std::size_t cursor = 0;
        for (auto& fragment : *group)
        {
            const std::size_t payload = fragment.size() - header;
            for (std::size_t offset = 0; offset < payload && cursor < record_size;
                 ++offset, ++cursor)
                fragment[header + offset] = logical[cursor];
            if (cursor >= record_size) break;
        }
        ++tampered_logical_;
        /* A single-occurrence rule is spent after one injection; an "every
         * occurrence" rule stays armed for the whole drive, by definition. */
        if (gatt_tamper_.occurrence != 0u) gatt_tamper_armed_ = false;
        return true;
    }

    /* True when this unit is a link-control READY/ACK frame from the direction
     * the filter holds. The cell layout is the repository's own app frame:
     * 4-byte big-endian length, then the 2-byte tag (wire/app_frame.cpp:480-482).
     * Nothing is rewritten: the frame is recognised, not changed. */
    [[nodiscard]] bool withholds(QuicFilter direction,
                                 const std::vector<std::uint8_t>& bytes) const
    {
        if (withheld_ == QuicFilter::None || withheld_ != direction) return false;
        if (bytes.size() < 6u) return false;
        const auto tag = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes[4]) << 8u) |
            static_cast<std::uint16_t>(bytes[5]));
        return tag == link::kLinkReadyObjectKindV1;
    }

private:
    struct QuicStream final
    {
        fly_session_resource_handle_v2 handle = 0;
        bool connector_registered = false;
        bool listener_registered = false;
    };

    void ensure_link_facts()
    {
        if (link_facts_ready_) return;
        link_facts_ready_ = true;
        std::array<std::uint8_t, 8> key{};
        for (int index = 0; index < 8; ++index)
            key[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(
                link_resource_ >> static_cast<unsigned>(56 - index * 8));
        static constexpr char kLabel[] = "flynes-loopback-quic-link-exporter-v1";
        exporter_ = loopback_hmac_sha256(
            key.data(), key.size(),
            reinterpret_cast<const std::uint8_t*>(kLabel), sizeof(kLabel) - 1);
        check(std::any_of(exporter_.begin(), exporter_.end(),
                          [](std::uint8_t value) { return value != 0; }),
              "the link's exporter is 32 bytes of the link's own material");
    }

    void deliver_connection(EngineFixture& fixture, std::uint64_t physical)
    {
        fly_session_port_event_v2 event{};
        event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
        event.abi_version = FLY_SESSION_ABI_VERSION_2;
        event.token = fixture.discovery.last_token;
        event.event_sequence = 1;
        event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
        event.terminal = 1;
        event.result = FLY_SESSION_V2_OK;
        event.payload_kind = FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2;
        fly_session_provider_resource_event_v2 payload{};
        payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
        payload.abi_version = FLY_SESSION_ABI_VERSION_2;
        payload.resource = link_resource_;
        payload.generation = event.token.connection_generation;
        payload.value0 = physical;
        payload.value1 = 23;
        event.payload_size = sizeof(payload);
        std::memcpy(event.payload, &payload, sizeof(payload));
        check(fly_session_deliver_v2(fixture.discovery.inbox, &event) ==
                  FLY_SESSION_V2_ACCEPTED,
              "the transport's own discovery connection enters the engine");
    }

    EngineFixture* peripheral_ = nullptr;
    EngineFixture* central_ = nullptr;
    /* One link, one handle: both ends are told they are on this same link. */
    fly_session_resource_handle_v2 link_resource_ = 0x5100;
    int connections_ = 0;
    bool peripheral_connected_ = false;
    bool central_connected_ = false;
    /* Step 5: the link's own QUIC facts and provider-owned object table. */
    EngineFixture* quic_listener_ = nullptr;
    std::vector<QuicStream> streams_{};
    fly_session_resource_handle_v2 next_quic_handle_ = 0x6100;
    std::size_t connector_streams_ = 0;
    std::size_t listener_streams_ = 0;
    std::array<std::uint8_t, 32> exporter_{};
    std::array<std::uint8_t, 32> pin_{};
    bool link_facts_ready_ = false;
    QuicFilter withheld_ = QuicFilter::None;
    /* W3 tamper matrix state. */
    std::array<std::uint8_t, 32> pin_override_{};
    bool has_pin_override_ = false;
    TamperRule tamper_{};
    bool tamper_armed_ = false;
    std::uint32_t tamper_occurrences_ = 0;
    std::uint32_t tampered_units_ = 0;
    std::uint32_t tampered_index_ = 0;
    std::uint8_t tampered_before_ = 0;
    std::uint8_t tampered_after_ = 0;
    /* W3 tamper matrix: the GATT logical-message fault. */
    GattTamperRule gatt_tamper_{};
    bool gatt_tamper_armed_ = false;
    std::uint32_t gatt_tamper_occurrences_ = 0;
    std::uint32_t tampered_logical_ = 0;
    std::uint32_t gatt_tamper_index_ = 0;
    std::uint8_t gatt_tamper_before_ = 0;
    std::uint32_t gatt_reassembly_mismatches_ = 0;
    std::uint32_t tampered_groups_delivered_ = 0;
};

/*
 * ------------------------------------------------------------------------- *
 * Increment 2 of the two-engine driver: the counted, bounded provider pump.
 *
 * WHAT THE PUMP IS ALLOWED TO DO
 *   Answer CLASS-1 operation terminals, and nothing else. It never delivers a
 *   connection, never delivers bytes, never advances a stage and never decides an
 *   outcome: those are the transport's and the engine's jobs. Every answer below
 *   is the provider's own report about work the engine asked *this* engine to do.
 *
 * WHAT IT IS NOT ALLOWED TO DO
 *   Invent an answer for an operation no port asked for. The pump is driven purely
 *   by the fixture's own per-callback counters, so an answer can only exist for a
 *   request that really happened, and the counts it reports are exactly the
 *   requests this phase produced. There is no "answer whatever comes next" path.
 *
 * BOUNDS
 *   `PumpLimits` is a hard ceiling. Exceeding it is a FAILURE, never a hang and
 *   never a silent stop: an engine that keeps asking for unbounded provider work
 *   is a defect, and a test that spun forever would hide it.
 *
 * IDEMPOTENCE
 *   The state records how many operations of each kind it has already answered, so
 *   re-running the pump can never answer the same terminal twice (a repeat is
 *   judged STALE by the engine, which would be a false failure).
 *
 * THE KEY AND CRYPTO PORTS
 *   They are answered here too, from the shared `LoopbackWorld`. A provider callback
 *   may not complete its own operation: the engine refuses a terminal delivered from
 *   inside the callback, and the refusal is silent, so the operation would stay
 *   pending forever. The callbacks therefore only record their arguments, and every
 *   completion is handed out below, driven exclusively by the port's own counter.
 *
 * PAYLOAD FORM
 *   Each completion uses the form `shared/src/session/ports/provider_events.cpp`'s
 *   `contract_for` requires for that kind - resource, buffer, hash or end - and the
 *   terminal flag that form implies. A kind delivered in the wrong form is rejected
 *   with CONTRACT_VIOLATION, which is also silent unless the delivery is asserted,
 *   so `loopback_deliver_*` asserts every result.
 * ------------------------------------------------------------------------- */

struct PumpLimits final
{
    /*
     * One round answers at most ONE provider request, so this bound is "how many
     * requests one engine may have outstanding inside a single call". The pair
     * exchange, the durable persistence and the link handshake together produce
     * several hundred legitimate requests per engine (measured: 270 and 249 answered
     * in the step-4 run, more once the QUIC stage is reached), so a small bound fails
     * a healthy engine. It stays a HARD failure: a burst beyond this is a defect, and
     * spinning forever would hide it.
     */
    int max_rounds = 4096;
    int max_answers = 4096;
};

/*
 * Per-port counts of what the pump actually answered. A test asserts these to show
 * that the pump answered precisely the requests the ports really made, rather than
 * a scripted guess at what should happen next.
 */
struct PumpCounts final
{
    int rounds = 0;
    int answers = 0;
    int key_handles = 0;
    int key_publics = 0;
    int key_agreements = 0;
    int key_signatures = 0;
    int crypto_randoms = 0;
    int crypto_secrets = 0;
    int crypto_macs = 0;
    int crypto_verifies = 0;
    /* Sub-counts of `crypto_verifies` and `crypto_aead_opens`: a rejection is still
     * an answer to the request the engine really made. */
    int crypto_verify_failures = 0;
    int crypto_aead_open_failures = 0;
    int crypto_aead_seals = 0;
    int crypto_aead_opens = 0;
    int tls_materials = 0;
    int bearer_capabilities = 0;
    int bearer_paths = 0;
    int bearer_credentials = 0;
    int bearer_endpoints = 0;
    int discovery_write_ends = 0;
    int secure_store_revisions = 0;
    int object_puts = 0;
    int object_reads = 0;
    /*
     * Step 5: what the pump answered on the QUIC port. `quic_reads` is the one
     * QUIC request the pump does NOT answer: a granted read credit is answered by
     * the TRANSPORT with the peer's own stream bytes, so this counter exists only
     * to prove the pump saw exactly the reads the engine issued (see
     * check_pump_answers_match_the_ports).
     */
    int quic_connections = 0;
    int quic_handshakes = 0;
    int quic_exporters = 0;
    int quic_streams = 0;
    int quic_write_ends = 0;
    int quic_reads = 0;
};

/* Provider-side bookkeeping: how much of each port's work is already answered. */
struct PumpState final
{
    PumpCounts counts{};
    int key_generates = 0;
    int key_public_reads = 0;
    int key_agreements = 0;
    int key_signs = 0;
    int crypto_randoms = 0;
    int crypto_hkdfs = 0;
    int crypto_verifies = 0;
    int crypto_hmacs = 0;
    int crypto_seals = 0;
    int crypto_opens = 0;
    int tls_creates = 0;
    int bearer_probes = 0;
    int bearer_creates = 0;
    int bearer_joins = 0;
    int bearer_prepares = 0;
    int bearer_resolves = 0;
    int discovery_writes = 0;
    int secure_store_writes = 0;
    int object_puts = 0;
    int object_reads = 0;
    /*
     * Step 5: the QUIC port. `pump_once` answers the connection, the handshake
     * inspection, the exporter, the bidi stream and the write here; a granted
     * read credit is deliberately left pending, because the operation that
     * completes it is the TRANSPORT delivering the peer engine's own bytes as a
     * class-2 QUIC_DATA event. These mirrors hold how much of each request is
     * already answered (or, for reads, already accounted for) so a repeated pass
     * can never answer the same terminal twice - a repeat is judged STALE by the
     * engine, which would be a false failure.
     */
    int quic_listens = 0;
    int quic_connects = 0;
    int quic_inspections = 0;
    int quic_exporters = 0;
    int quic_accepted_bidi = 0;
    int quic_opened_bidi = 0;
    int quic_writes = 0;
    int quic_reads = 0;
    int quic_cancels = 0;
    int content_queries = 0;
    /* Deterministic, non-zero resource handles for provider-owned objects. */
    fly_session_resource_handle_v2 next_handle = 0x4000;
};

enum class PumpOutcome : std::uint8_t
{
    /* Exactly one class-1 terminal was answered; the engine may now progress. */
    Answered = 1,
    /*
     * Every class-1 request this engine has made is answered. The engine is now
     * waiting for something the transport has not delivered: in this increment that
     * is peer bytes, which the GATT byte loopback (increment 3) and the QUIC byte
     * loopback (increment 4) supply. Being idle here is not an error.
     */
    Idle = 2
};

/*
 * The credential a provider mints for an initial bearer. `codec` is the plan's own
 * credential codec, so the shape has to follow the plan rather than be chosen
 * here. The value is derived only from the plan, which both ends agree on, so the
 * creator's and the receiver's canonical join parameters come out identical - which
 * is what `Stage::PrepareReceiver` requires when it compares them byte for byte.
 */
inline std::array<std::uint8_t, flynes::session::wire::kBearerCredentialBytesSizeV1>
loopback_bearer_credential(std::uint8_t codec)
{
    std::array<std::uint8_t,
               flynes::session::wire::kBearerCredentialBytesSizeV1> credential{};
    credential[0] = 1;
    if (codec == 1)
    {
        /* Codec 1: two printable, zero-padded text fields (lengths 1..32 and
         * 8..63). */
        credential[1] = 1;
        credential[2] = 16;
        credential[3] = 8;
        std::fill_n(credential.begin() + 4, 16, std::uint8_t{0x41});
        std::fill_n(credential.begin() + 36, 8, std::uint8_t{0x42});
        return credential;
    }
    /* Codec 2: one opaque non-zero field and no second field. */
    credential[1] = 1;
    credential[2] = 16;
    credential[3] = 0;
    std::fill_n(credential.begin() + 4, 16, std::uint8_t{0xc1});
    return credential;
}

/*
 * Answers at most one pending class-1 request, chosen by which port counter grew.
 * Because the engine dispatches a single effect per iteration and then continues,
 * at most one port can have an unanswered request at a time, so a single pass in a
 * fixed port order is exact.
 */
inline PumpOutcome pump_once(EngineFixture& fixture, PumpState& state)
{
    fixture.executor.run_all();

    if (fixture.tls.creates > state.tls_creates)
    {
        ++state.tls_creates;
        const auto handle = state.next_handle++;
        /* TLS_MATERIAL_V2 is a HASH form completion whose hash must be the digest of
         * the key's public bytes: the scheduler compares it against the SPKI it read
         * through the key port, and rejects anything else as AUTH_FAILED. */
        const auto* point = fixture.key.world != nullptr
            ? fixture.key.world->point_of(fixture.tls.last_key)
            : nullptr;
        check(point != nullptr,
              "the provider mints TLS material for a key the shared world really "
              "holds");
        std::array<std::uint8_t, 32> hash{};
        if (point != nullptr)
            hash = wire::sha256(point->data(), point->size());
        /* The SPKI hash the engine is told it observed. The link reads it back
         * for the connector's handshake facts, so both ends of the link agree on
         * the listener's identity material without either one asserting a value
         * the other never produced. */
        fixture.tls.last_spki_hash = hash;
        deliver_provider_hash(fixture.tls.inbox, fixture.tls.last_token,
                              FLY_SESSION_PROVIDER_TLS_MATERIAL_V2, handle, hash);
        ++state.counts.tls_materials;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.bearer.probes > state.bearer_probes)
    {
        ++state.bearer_probes;
        /* The provider reports the bearer capabilities it really has. */
        deliver_capability(fixture);
        ++state.counts.bearer_capabilities;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.bearer.creates > state.bearer_creates ||
        fixture.bearer.joins > state.bearer_joins)
    {
        const bool creator = fixture.bearer.creates > state.bearer_creates;
        if (creator) ++state.bearer_creates; else ++state.bearer_joins;
        const auto handle = state.next_handle++;
        deliver_provider_resource(fixture.bearer.inbox, fixture.bearer.last_token,
                                  FLY_SESSION_PROVIDER_BEARER_PATH_V2, handle);
        ++state.counts.bearer_paths;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.bearer.prepares > state.bearer_prepares)
    {
        ++state.bearer_prepares;
        check(fixture.bearer.last_plan.size() ==
                  sizeof(flynes::session::wire::BearerPlanBytes),
              "the provider is asked to prepare a credential for a 48-byte plan");
        flynes::session::wire::BearerPlanBytes plan{};
        std::copy(fixture.bearer.last_plan.begin(),
                  fixture.bearer.last_plan.end(), plan.begin());
        const auto credential = loopback_bearer_credential(plan[3]);
        std::array<std::uint8_t,
                   flynes::session::wire::kBearerJoinParamsSizeV1> join_params{};
        /* The lifetime is a fixed provider policy value, not a clock reading, so
         * both ends of the loopback publish byte-identical join parameters. */
        constexpr std::uint32_t kLoopbackCredentialValidForMs = 60000;
        check(flynes::session::wire::encode_bearer_join_params_v1(
                  plan, kLoopbackCredentialValidForMs, credential, &join_params) ==
                  flynes::session::wire::Status::Ok,
              "the provider encodes canonical join parameters for its own plan");
        /* W3 tamper: the credential this provider minted no longer matches the
         * plan, so the receiving engine's own comparison must reject it. The
         * hash below is the digest of exactly these tampered bytes, so the
         * receiving engine's transport-level hash check cannot be what catches
         * it - only its canonical join-parameter comparison can. */
        if (fixture.bearer.tamper_join_params && join_params.size() > 4u)
        {
            join_params[4] = static_cast<std::uint8_t>(
                join_params[4] ^ fixture.bearer.tamper_join_params_mask);
            ++fixture.bearer.join_params_tampered;
        }
        const auto hash = flynes::session::wire::sha256(join_params.data(),
                                                       join_params.size());
        const auto handle = state.next_handle++;
        deliver_provider_hash_buffer(
            fixture.bearer.inbox, fixture.bearer.last_token,
            FLY_SESSION_PROVIDER_BEARER_CREDENTIAL_V2, handle,
            join_params.data(), join_params.size(), hash);
        ++state.counts.bearer_credentials;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.bearer.resolves > state.bearer_resolves)
    {
        ++state.bearer_resolves;
        /* One loopback link has one endpoint; this is the address the peer's QUIC
         * listener is reachable at, which is what `resolve` asks for. */
        std::array<std::uint8_t, 18> endpoint{};
        endpoint[12] = 192;
        endpoint[13] = 168;
        endpoint[14] = 1;
        endpoint[15] = 9;
        endpoint[16] = 0xd6;
        endpoint[17] = 0xd8;
        deliver_provider_buffer(fixture.bearer.inbox, fixture.bearer.last_token,
                                FLY_SESSION_PROVIDER_BEARER_ENDPOINT_V2,
                                endpoint.data(), endpoint.size());
        ++state.counts.bearer_endpoints;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.discovery.writes > state.discovery_writes)
    {
        ++state.discovery_writes;
        /* Completing the local write effect. The bytes themselves are read back by
         * the peer through the transport, not from here. */
        deliver_discovery_end(fixture);
        ++state.counts.discovery_write_ends;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.secure_store.writes > state.secure_store_writes)
    {
        ++state.secure_store_writes;
        const auto handle = state.next_handle++;
        deliver_provider_resource(fixture.secure_store.inbox,
                                  fixture.secure_store.last_token,
                                  FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2,
                                  handle);
        ++state.counts.secure_store_revisions;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.object_store.puts > state.object_puts)
    {
        ++state.object_puts;
        /* The store reports the object it durably holds. It answers with the
         * content hash the caller declared for the bytes it just handed over,
         * which is the pair this store recorded; the read path below returns those
         * same bytes, and the engine's own re-read verification is what proves the
         * pair is the one it asked for. */
        const auto handle = state.next_handle++;
        deliver_provider_hash(fixture.object_store.inbox,
                             fixture.object_store.last_token,
                             FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, handle,
                             fixture.object_store.last_hash);
        ++state.counts.object_puts;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.object_store.reads > state.object_reads)
    {
        ++state.object_reads;
        /* `read` already located the retained object for the requested
         * (kind, hash); a request this store cannot serve returned UNAVAILABLE
         * synchronously and never reached here. */
        const auto found = std::find_if(
            fixture.object_store.stored.begin(), fixture.object_store.stored.end(),
            [&](const EngineFixture::ObjectStore::StoredObject& item) {
                return item.kind == fixture.object_store.last_read_kind &&
                       std::equal(item.hash.begin(), item.hash.end(),
                                  fixture.object_store.last_read_hash.begin());
            });
        check(found != fixture.object_store.stored.end(),
              "the provider answers only a re-read of an object it really stored");
        if (found != fixture.object_store.stored.end())
        {
            const auto handle = state.next_handle++;
            deliver_provider_hash_buffer(
                fixture.object_store.read_inbox,
                fixture.object_store.last_read_token,
                FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, handle,
                found->value.data(), found->value.size(), found->hash);
            ++state.counts.object_reads;
            ++state.counts.answers;
            return PumpOutcome::Answered;
        }
    }

    /*
     * --------------------------------------------------------------------- *
     * The key and crypto ports.
     *
     * These operations are answered HERE, and never from inside the provider
     * callback that requested them: the engine refuses a terminal delivered from
     * within a callback (the completion record does not exist yet), the refusal is
     * silent, and the operation would stay pending forever. Each branch below is
     * driven only by "this port's own counter grew", so the pump can never answer an
     * operation no port requested, and each completion uses the payload form
     * `contract_for` declares for its kind.
     *
     * The world is the same deterministic object both engines were built on, so the
     * two ends derive matching material without exchanging these values. It is an
     * ECDH/AEAD/HMAC stand-in: this pump certifies the protocol pipeline, never
     * cryptography.
     * --------------------------------------------------------------------- *
     */
    const bool key_or_crypto_pending =
        fixture.key.generates > state.key_generates ||
        fixture.key.public_reads > state.key_public_reads ||
        fixture.key.agreements > state.key_agreements ||
        fixture.key.signs > state.key_signs ||
        fixture.crypto.randoms > state.crypto_randoms ||
        fixture.crypto.hkdfs > state.crypto_hkdfs ||
        fixture.crypto.verifies > state.crypto_verifies ||
        fixture.crypto.hmacs > state.crypto_hmacs ||
        fixture.crypto.aead_seals > state.crypto_seals ||
        fixture.crypto.aead_opens > state.crypto_opens;
    if (key_or_crypto_pending &&
        (fixture.key.world == nullptr || fixture.crypto.world == nullptr))
    {
        check(false,
              "the pump cannot answer an unanswered key or crypto request because no "
              "shared loopback world is attached to the engine");
        return PumpOutcome::Idle;
    }

    if (fixture.key.generates > state.key_generates)
    {
        const auto& request = fixture.key.generate_requests[
            static_cast<std::size_t>(state.key_generates)];
        ++state.key_generates;
        /* KEY_HANDLE_V2 is a HASH form completion: the handle the WORLD allocated for
         * this (purpose, side) point, together with the digest of that point. The
         * handle must be the one the world itself allocated - the world resolves it
         * again for every later public_key/agree/sign use - so it is allocated here
         * and then looked up, rather than re-derived from the digest. */
        const auto handle = fixture.key.world->allocate_point(
            EngineFixture::Key::point_index_for(request.purpose,
                                                fixture.key.side));
        const auto* point = fixture.key.world->point_of(handle);
        check(point != nullptr,
              "the shared world holds the point it just allocated for the key "
              "request");
        if (point == nullptr) return PumpOutcome::Idle;
        const auto hash = wire::sha256(point->data(), point->size());
        deliver_provider_hash(fixture.key.inbox, request.token,
                              FLY_SESSION_PROVIDER_KEY_HANDLE_V2, handle, hash);
        ++state.counts.key_handles;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.key.public_reads > state.key_public_reads)
    {
        const auto& request = fixture.key.public_requests[
            static_cast<std::size_t>(state.key_public_reads)];
        ++state.key_public_reads;
        /* KEY_PUBLIC_V2 is a BUFFER form completion carrying the peer-visible X9.63
         * public bytes of the key the engine asked about. */
        const auto* point = fixture.key.world->point_of(request.resource);
        check(point != nullptr,
              "the provider reports a public key only for a handle the shared world "
              "really holds");
        if (point == nullptr) return PumpOutcome::Idle;
        deliver_provider_buffer(fixture.key.inbox, request.token,
                                FLY_SESSION_PROVIDER_KEY_PUBLIC_V2, point->data(),
                                point->size());
        ++state.counts.key_publics;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.key.agreements > state.key_agreements)
    {
        const auto& request = fixture.key.agree_requests[
            static_cast<std::size_t>(state.key_agreements)];
        ++state.key_agreements;
        const auto* mine = fixture.key.world->point_of(request.resource);
        check(mine != nullptr && request.peer.size() == 65u,
              "the provider agrees a key it holds against a 65-byte peer point");
        if (mine == nullptr || request.peer.size() != 65u)
            return PumpOutcome::Idle;
        LoopbackPoint theirs{};
        std::copy_n(request.peer.data(), theirs.size(), theirs.begin());
        /* ECDH STAND-IN, NOT ECDH. `LoopbackWorld::agree` hashes the two public keys
         * under its own domain: both ends derive the same secret, which is what the
         * protocol layer needs, but it implements no curve arithmetic and verifies no
         * NIST P-256 key-agreement property. Real key agreement is certified by the
         * provider/device tests, never by this harness. */
        const auto secret = fixture.key.world->agree(*mine, theirs);
        const auto handle = fixture.key.world->allocate_secret(secret);
        deliver_provider_resource(fixture.key.inbox, request.token,
                                  FLY_SESSION_PROVIDER_KEY_AGREEMENT_V2, handle);
        ++state.counts.key_agreements;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.key.signs > state.key_signs)
    {
        const auto& request = fixture.key.sign_requests[
            static_cast<std::size_t>(state.key_signs)];
        ++state.key_signs;
        /* KEY_SIGNATURE_V2 is a BUFFER form completion: the 64-byte canonical low-S
         * signature the world really recomputed over the requested digest. */
        const auto* point = fixture.key.world->point_of(request.resource);
        check(point != nullptr,
              "the provider signs with a handle the shared world really holds");
        if (point == nullptr) return PumpOutcome::Idle;
        auto signature = fixture.key.world->sign(
            *point, request.domain.data(), request.domain.size(),
            request.digest.data());
        deliver_provider_buffer(fixture.key.inbox, request.token,
                                FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2,
                                signature.data(), signature.size());
        ++state.counts.key_signatures;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.crypto.randoms > state.crypto_randoms)
    {
        const auto& request = fixture.crypto.random_requests[
            static_cast<std::size_t>(state.crypto_randoms)];
        ++state.crypto_randoms;
        /* CRYPTO_RANDOM_V2 is a BUFFER form completion whose bytes the world derives
         * from the side, the request counter and the purpose it was asked with. */
        const auto bytes = fixture.crypto.world->random(
            fixture.crypto.side, request.size, request.purpose.data(),
            request.purpose.size());
        deliver_provider_buffer(fixture.crypto.inbox, request.token,
                                FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2, bytes.data(),
                                bytes.size());
        ++state.counts.crypto_randoms;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.crypto.hkdfs > state.crypto_hkdfs)
    {
        const auto& request = fixture.crypto.hkdf_requests[
            static_cast<std::size_t>(state.crypto_hkdfs)];
        ++state.crypto_hkdfs;
        /* CRYPTO_SECRET_V2 is a RESOURCE form completion: the derived secret is
         * referenced by a handle the world allocated for it. */
        const auto* material =
            fixture.crypto.world->secret_of(request.resource);
        check(material != nullptr,
              "the provider derives from a secret the shared world really holds");
        if (material == nullptr) return PumpOutcome::Idle;
        const auto derived = fixture.crypto.world->hkdf(
            *material, request.salt.data(), request.salt.size(),
            request.info.data(), request.info.size(), request.size);
        const auto handle = fixture.crypto.world->allocate_secret(derived);
        deliver_provider_resource(fixture.crypto.inbox, request.token,
                                  FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, handle);
        ++state.counts.crypto_secrets;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.crypto.hmacs > state.crypto_hmacs)
    {
        const auto& request = fixture.crypto.hmac_requests[
            static_cast<std::size_t>(state.crypto_hmacs)];
        ++state.crypto_hmacs;
        /* CRYPTO_MAC_V2 is a BUFFER form completion: the 32-byte tag. */
        const auto* material =
            fixture.crypto.world->secret_of(request.resource);
        check(material != nullptr,
              "the provider macs with a secret the shared world really holds");
        if (material == nullptr) return PumpOutcome::Idle;
        const auto tag = fixture.crypto.world->hmac(
            *material, request.input.data(), request.input.size());
        deliver_provider_buffer(fixture.crypto.inbox, request.token,
                                FLY_SESSION_PROVIDER_CRYPTO_MAC_V2, tag.data(),
                                tag.size());
        ++state.counts.crypto_macs;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.crypto.verifies > state.crypto_verifies)
    {
        const auto& request = fixture.crypto.verify_requests[
            static_cast<std::size_t>(state.crypto_verifies)];
        ++state.crypto_verifies;
        /* CRYPTO_VERIFICATION_V2 is an END form completion whose RESULT is the
         * verdict. The verifier really recomputes the signature instead of accepting
         * anything, so a tampered signature is reported AUTH_FAILED. */
        LoopbackPoint key{};
        std::copy_n(request.public_key.data(), key.size(), key.begin());
        const bool ok = fixture.crypto.world->verify(
            key, request.info.data(), request.info.size(),
            request.digest.data(), request.signature.data());
        if (!ok) ++state.counts.crypto_verify_failures;
        loopback_deliver_verification(
            fixture.crypto.inbox, request.token,
            ok ? FLY_SESSION_V2_OK : FLY_SESSION_V2_AUTH_FAILED);
        ++state.counts.crypto_verifies;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.crypto.aead_seals > state.crypto_seals)
    {
        const auto& request = fixture.crypto.seal_requests[
            static_cast<std::size_t>(state.crypto_seals)];
        ++state.crypto_seals;
        /* CRYPTO_AEAD_V2 is a BUFFER form completion: ciphertext with its tag. */
        const auto* material =
            fixture.crypto.world->secret_of(request.resource);
        check(material != nullptr,
              "the provider seals under a secret the shared world really holds");
        if (material == nullptr) return PumpOutcome::Idle;
        auto sealed = fixture.crypto.world->seal(
            *material, request.nonce.data(), request.nonce.size(),
            request.aad.data(), request.aad.size(), request.input.data(),
            request.input.size());
        deliver_provider_buffer(fixture.crypto.inbox, request.token,
                                FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2, sealed.data(),
                                sealed.size());
        ++state.counts.crypto_aead_seals;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.crypto.aead_opens > state.crypto_opens)
    {
        const auto& request = fixture.crypto.open_requests[
            static_cast<std::size_t>(state.crypto_opens)];
        ++state.crypto_opens;
        /* CRYPTO_AEAD_V2 on the open side is a BUFFER form completion when the tag
         * verifies, and an END form completion carrying AUTH_FAILED when it does not.
         * A refusal is a real provider verdict, not a fabricated failure. */
        const auto* material =
            fixture.crypto.world->secret_of(request.resource);
        check(material != nullptr,
              "the provider opens under a secret the shared world really holds");
        if (material == nullptr) return PumpOutcome::Idle;
        std::vector<std::uint8_t> opened;
        const bool ok = fixture.crypto.world->open(
            *material, request.nonce.data(), request.nonce.size(),
            request.aad.data(), request.aad.size(), request.input.data(),
            request.input.size(), &opened);
        if (ok)
            deliver_provider_buffer(fixture.crypto.inbox, request.token,
                                    FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
                                    opened.data(), opened.size());
        else
        {
            ++state.counts.crypto_aead_open_failures;
            loopback_deliver_end(fixture.crypto.inbox, request.token,
                                 FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
                                 FLY_SESSION_V2_AUTH_FAILED);
        }
        ++state.counts.crypto_aead_opens;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    /*
     * --------------------------------------------------------------------- *
     * The QUIC port (step 5).
     *
     * Every answer below is driven by the port's own counter, so an answer can
     * only exist for a request this engine really made, and each uses the payload
     * form and terminal flag `shared/src/session/ports/provider_events.cpp`'s
     * `contract_for` declares for that kind:
     *
     *   listen/connect   -> QUIC_CONNECTION_V2, RESOURCE form, terminal 1
     *   inspect_handshake-> QUIC_HANDSHAKE_V2,  HASH form,   terminal 1
     *   exporter         -> QUIC_EXPORTER_V2,   BUFFER form, terminal 1, 32 bytes
     *   open/accept_bidi -> QUIC_STREAM_V2,     RESOURCE form, terminal 1
     *   write            -> QUIC_END_V2,        END form,    terminal 1
     *   grant_read_credit-> NO terminal: the operation is completed by the
     *                       transport's own QUIC_DATA event, which is why the
     *                       counter below is mirrored without being answered.
     *
     * The link-wide values (handshake facts, exporter, stream handles) come from
     * `LoopbackTransport`, never from a constant in this file and never from a
     * peer claim.
     * --------------------------------------------------------------------- */
    const bool quic_pending =
        fixture.quic.listens > state.quic_listens ||
        fixture.quic.connects > state.quic_connects ||
        fixture.quic.inspections > state.quic_inspections ||
        fixture.quic.exporters > state.quic_exporters ||
        fixture.quic.accepted_bidi > state.quic_accepted_bidi ||
        fixture.quic.opened_bidi > state.quic_opened_bidi ||
        fixture.quic.writes > state.quic_writes ||
        fixture.quic.reads > state.quic_reads ||
        fixture.quic.cancels > state.quic_cancels;
    if (quic_pending && fixture.attached_link == nullptr)
    {
        check(false,
              "the engine asked the QUIC port for work but is not attached to a "
              "loopback link, so no handshake facts, stream or exporter can be "
              "truthfully reported");
        return PumpOutcome::Idle;
    }

    if (fixture.quic.listens > state.quic_listens ||
        fixture.quic.connects > state.quic_connects)
    {
        const bool listener = fixture.quic.listens > state.quic_listens;
        if (listener)
        {
            ++state.quic_listens;
            fixture.attached_link->note_quic_listener(fixture);
        }
        else
        {
            ++state.quic_connects;
        }
        /* A real transport answers a started connection with a provider-owned
         * connection handle, which every later inspect/exporter on this link
         * refers to. */
        const auto handle = fixture.attached_link->allocate_quic_handle();
        deliver_provider_resource(fixture.quic.inbox, fixture.quic.last_token,
                                  FLY_SESSION_PROVIDER_QUIC_CONNECTION_V2, handle);
        ++state.counts.quic_connections;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.quic.inspections > state.quic_inspections)
    {
        ++state.quic_inspections;
        /* QUIC_HANDSHAKE_V2 is a HASH form completion whose buffer is the link's
         * own encoded handshake facts and whose hash is sha256 of exactly those
         * bytes - the engine recomputes it and rejects anything else as
         * CONTRACT_VIOLATION (initial_quic_bind_scheduler.cpp:398-401). */
        const bool listener = fixture.quic.listens > 0;
        std::vector<std::uint8_t> facts;
        check(fixture.attached_link->quic_handshake_facts(fixture, listener,
                                                          &facts),
              "the link encodes its own handshake facts with the repository's own "
              "encoder");
        const auto hash = wire::sha256(facts.data(), facts.size());
        fixture.quic.last_handshake_hash = hash;
        ++fixture.quic.handshakes;
        deliver_provider_hash_buffer(
            fixture.quic.inbox, fixture.quic.last_token,
            FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2, fixture.quic.last_resource,
            facts.data(), facts.size(), hash);
        ++state.counts.quic_handshakes;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.quic.exporters > state.quic_exporters)
    {
        ++state.quic_exporters;
        /* The engine asks for the exporter by its exact label and a context
         * derived from the pair transcript; the link answers with the ONE value
         * both ends must share, or the two engines would derive different
         * channel ids and neither bind proof could verify. */
        static constexpr char kExporterLabel[] = "EXPORTER-flynes-nearby-v1";
        const std::vector<std::uint8_t> expected_label(
            kExporterLabel, kExporterLabel + sizeof(kExporterLabel) - 1);
        check(fixture.quic.last_label == expected_label,
              "the engine asks the QUIC port for the link's own exporter label");
        check(fixture.quic.last_context.size() == 32,
              "the exporter is requested under a 32-byte channel context");
        std::array<std::uint8_t, 32> exporter = fixture.attached_link->exporter();
        /* W3 tamper: this end is told a different exporter than its peer. */
        if (fixture.quic.use_exporter_override)
        {
            exporter = fixture.quic.exporter_override;
            ++fixture.quic.exporter_overrides;
        }
        fixture.quic.last_exporter = exporter;
        ++fixture.quic.exporter_results;
        deliver_provider_buffer(fixture.quic.inbox, fixture.quic.last_token,
                                FLY_SESSION_PROVIDER_QUIC_EXPORTER_V2,
                                exporter.data(), exporter.size());
        ++state.counts.quic_exporters;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (static_cast<std::size_t>(state.content_queries) <
        fixture.content.queries.size())
    {
        const auto& request = fixture.content.queries[
            static_cast<std::size_t>(state.content_queries)];
        ++state.content_queries;
        const auto record = loopback_content_choice_record_v1();
        const auto hash = loopback_content_choice_hash_v1();
        deliver_provider_hash_buffer(
            fixture.content.inbox, request.token,
            FLY_SESSION_PROVIDER_CONTENT_CHOICE_V2,
            static_cast<fly_session_resource_handle_v2>(request.index + 1u),
            record.data(), record.size(), hash);
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.quic.opened_bidi > state.quic_opened_bidi ||
        fixture.quic.accepted_bidi > state.quic_accepted_bidi)
    {
        const bool listener = fixture.quic.accepted_bidi > state.quic_accepted_bidi;
        if (listener) ++state.quic_accepted_bidi;
        else ++state.quic_opened_bidi;
        /* The link owns the stream table: the Nth stream either role registers is
         * the same physical stream, and the two handles the answer carries are
         * documented on LoopbackTransport::register_quic_stream. */
        const auto answer = fixture.attached_link->register_quic_stream(listener);
        deliver_provider_resource_pair(
            fixture.quic.inbox, fixture.quic.last_token,
            FLY_SESSION_PROVIDER_QUIC_STREAM_V2, answer.send, answer.receive);
        ++state.counts.quic_streams;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.quic.writes > state.quic_writes)
    {
        ++state.quic_writes;
        /* QUIC_END_V2 is an END form completion, terminal 1: the provider reports
         * that the bytes the engine handed over were really written. The bytes
         * themselves are carried by the transport, straight from the engine's own
         * write log. */
        deliver_provider_end(fixture.quic.inbox, fixture.quic.last_token,
                             FLY_SESSION_PROVIDER_QUIC_END_V2);
        ++state.counts.quic_write_ends;
        ++state.counts.answers;
        return PumpOutcome::Answered;
    }

    if (fixture.quic.reads > state.quic_reads)
    {
        /* A granted read credit is NOT answered here: the operation stays pending
         * until the peer engine's own bytes arrive as a QUIC_DATA event under this
         * exact token. Mirroring the count is what proves the pump saw precisely
         * the reads the engine issued. */
        state.quic_reads = fixture.quic.reads;
        ++state.counts.quic_reads;
    }

    if (fixture.quic.cancels > state.quic_cancels)
    {
        state.quic_cancels = fixture.quic.cancels;
    }

    /* Nothing is pending that this pump can answer. */
    return PumpOutcome::Idle;
}

/*
 * Runs `pump_once` until the engine is idle, failing hard if a bound is exceeded.
 *
 * The bounds are PER CALL, not cumulative: the driver pumps the same engine once per
 * round for as many rounds as the exchange needs, and a cumulative bound would turn
 * ordinary progress into a failure. `state.counts.rounds` and `state.counts.answers`
 * stay cumulative because they are reported statistics, never the bound.
 */
inline void pump_engine(EngineFixture& fixture, PumpState& state,
                        PumpLimits limits = {})
{
    int rounds = 0;
    int answers = 0;
    for (;;)
    {
        if (rounds >= limits.max_rounds)
        {
            check(false,
                  "the pump exceeded max_rounds in one call: this engine kept asking "
                  "for more provider work than the bound allows");
            return;
        }
        ++rounds;
        ++state.counts.rounds;
        if (pump_once(fixture, state) == PumpOutcome::Idle) return;
        ++answers;
        if (answers > limits.max_answers)
        {
            check(false,
                  "the pump exceeded max_answers in one call: more terminals were "
                  "answered than the bound allows");
            return;
        }
    }
}

/*
 * ------------------------------------------------------------------------- *
 * Increment 4 of the two-engine driver: the byte-accurate GATT relay.
 *
 * WHAT CROSSES
 *   Only the source engine's OWN written fragments, verbatim, and only once the
 *   receiving engine has subscribed. Nothing here re-assembles, re-frames or
 *   re-encodes anything: what the peer engine receives is the byte-for-byte encoder
 *   output of the engine that wrote it, which is what makes the exchange a real one
 *   and what lets a test compare "what crossed" against the writer's own bytes.
 *
 * PACING
 *   One call carries at most ONE logical message and at most ONE physical
 *   acknowledgement fragment per direction, and the driver alternates carrying bytes
 *   with pumping provider work. The end of the current logical message is found by
 *   feeding the source's own fragments into a `GattReassembler`, which is used only
 *   as a locator: it never produces a byte that is delivered.
 *
 * THE RECEIVING END MUST BE LISTENING
 *   A fragment is delivered only when the sink has already subscribed. Bytes relayed
 *   before both engines have subscribed are lost to the receiving engine, and an
 *   earlier attempt that relayed early (and paced differently) deadlocked after
 *   PairContext + PairCommit with the scanning engine having written nothing.
 *
 * BACKPRESSURE
 *   BACKPRESSURE (-7) means "retry after the receiver is pumped", so the delivery
 *   watermark and the receiving inbox's event sequence are NOT advanced and the same
 *   fragment is attempted again later. Any other refusal is a harness defect and is
 *   reported with its numeric code rather than retried or hidden.
 * ------------------------------------------------------------------------- */

struct RelayDirectionState final
{
    /* Delivery watermarks into the source's own fragment lists. */
    std::size_t fragments_relayed = 0;
    std::size_t acks_relayed = 0;
    /* Class-2 event sequence for the RECEIVING inbox; strictly increasing. */
    std::uint64_t next_sequence = 1;
    /*
     * Exactly what crossed, in order, kept so a test can compare it with the writing
     * engine's own `written_fragments` instead of taking this harness's word for it.
     */
    std::vector<std::vector<std::uint8_t>> delivered;
    std::vector<std::vector<std::uint8_t>> delivered_acks;
    /*
     * W3 tamper matrix: fragments that must be delivered INSTEAD of the source's
     * own, stored per source-fragment index and PERSISTENTLY.
     *
     * This has to live in the direction's state rather than in a per-attempt
     * local: a BACKPRESSURE return abandons the attempt and the fragment is
     * offered again on a later round, so a tamper applied to a throw-away copy
     * would simply be discarded and the ORIGINAL bytes would cross - which is
     * exactly the bug that made three tamper cases look fail-open when nothing
     * had in fact been tampered on the wire.
     */
    std::vector<std::size_t> replaced_index;
    std::vector<std::vector<std::uint8_t>> replaced_bytes;
    int replaced_delivered = 0;

    void remember_replacement(std::size_t index, std::vector<std::uint8_t> bytes)
    {
        for (std::size_t slot = 0; slot < replaced_index.size(); ++slot)
            if (replaced_index[slot] == index)
            {
                replaced_bytes[slot] = std::move(bytes);
                return;
            }
        replaced_index.push_back(index);
        replaced_bytes.push_back(std::move(bytes));
    }

    [[nodiscard]] const std::vector<std::uint8_t>* replacement_at(
        std::size_t index) const
    {
        for (std::size_t slot = 0; slot < replaced_index.size(); ++slot)
            if (replaced_index[slot] == index) return &replaced_bytes[slot];
        return nullptr;
    }
};

struct QuicDirectionState final
{
    /* How much of the source's write log is already staged, and how many of the
     * sink's own granted reads have been answered. */
    std::size_t writes_staged = 0;
    std::size_t reads_answered = 0;
    std::vector<std::uint8_t> grants_consumed;
    /* Class-2 event sequence for the RECEIVING inbox; strictly increasing. */
    std::uint64_t next_sequence = 1;
    /*
     * One logical unit: the exact bytes of one write the source engine issued, or
     * the empty unit that carries the FIN it asked for. A non-empty write that
     * also asked for the FIN produces TWO units, because the reader learns about
     * the FIN by issuing a further read for it.
     */
    struct Unit final
    {
        fly_session_resource_handle_v2 stream = 0;
        std::vector<std::uint8_t> bytes;
        bool fin = false;
        bool held = false;
    };
    std::vector<Unit> pending;
    /*
     * Exactly what crossed, in order, kept so a test can compare it with the
     * writing engine's own write log instead of taking this harness's word for it.
     * `delivered` holds every unit (including FIN units, which are empty), and
     * `delivered_fin` says which of them were the FIN rather than payload bytes.
     */
    std::vector<std::vector<std::uint8_t>> delivered;
    std::vector<std::uint32_t> delivered_streams;
    std::vector<std::uint32_t> delivered_fin;
    int units = 0;
    int fins = 0;
    /* Units the transport's filter held back rather than delivered. */
    int held_units = 0;
};

struct RelayReport final
{
    RelayDirectionState peripheral_to_central{};
    RelayDirectionState central_to_peripheral{};
    /* Step 5: the two directions of the one QUIC connection. */
    QuicDirectionState quic_peripheral_to_central{};
    QuicDirectionState quic_central_to_peripheral{};
    /*
     * Every app action kind the two engines published during the drive, in the
     * order they were first seen. The driver reports them so a test can state
     * which actions this path really offers; it submits only CONFIRM_SAS, because
     * that is the only action the ABI defines as the app's own decision here, and
     * it never invents a response for one it does not understand.
     */
    std::vector<std::uint32_t> app_action_kinds;
    int rounds = 0;
    int app_actions = 0;
};

/*
 * One delivery attempt of one fragment as a class-2 DISCOVERY_BYTES event.
 *
 * `terminal` is 0 and the token is the receiving engine's own subscription token:
 * the engine accepts a byte event only under that exact token, and `last_token` has
 * been overwritten by every later write by the time the relay runs.
 */
inline fly_session_result_v2 deliver_relay_fragment(
    EngineFixture& sink, const std::vector<std::uint8_t>& fragment,
    std::uint64_t sequence)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{
        fragment.data(), static_cast<std::uint32_t>(fragment.size()), 0};
    check(fly_session_buffer_create_copy_v2(source, &buffer) == FLY_SESSION_V2_OK,
          "the relay owns an immutable copy of the fragment it carries");
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = sink.discovery.subscription_token;
    event.event_sequence = sequence;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 0;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2;
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = buffer;
    payload.logical_size = fragment.size();
    payload.generation = event.token.connection_generation;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    const auto result = fly_session_deliver_v2(sink.discovery.inbox, &event);
    fly_session_buffer_release_v2(buffer);
    return result;
}

/*
 * One attempt for one fragment. Returns true when the receiver ACCEPTED it (the
 * caller then advances its watermark); false when the receiver applied
 * BACKPRESSURE, which means the attempt must be repeated after it is pumped. Any
 * other result is reported with its numeric code.
 */
inline bool relay_fragment_once(EngineFixture& sink, RelayDirectionState& state,
                                const std::vector<std::uint8_t>& fragment,
                                std::vector<std::vector<std::uint8_t>>* record)
{
    const auto result = deliver_relay_fragment(sink, fragment, state.next_sequence);
    if (result == FLY_SESSION_V2_ACCEPTED)
    {
        ++state.next_sequence;
        record->push_back(fragment);
        return true;
    }
    if (result == FLY_SESSION_V2_BACKPRESSURE) return false;
    char message[512];
    const auto snapshot = sink.snapshot();
    std::snprintf(message, sizeof(message),
                  "the receiving engine refused a loopback GATT byte event with "
                  "result %d, which is neither ACCEPTED nor BACKPRESSURE (its link "
                  "state is %u, reason '%s')",
                  static_cast<int>(result),
                  static_cast<unsigned>(snapshot.link_state),
                  snapshot.primary_reason_key);
    check(false, message);
    return false;
}

/*
 * Carries at most one logical message and at most one physical acknowledgement
 * fragment from `source` to `sink`, in that order. `source_is_peripheral` selects the
 * physical direction the locator reassembles under, which is the direction the
 * SOURCE wrote in; the bytes themselves are never touched.
 */
inline void relay_one_direction(EngineFixture& source, RelayDirectionState& state,
                                EngineFixture& sink, bool source_is_peripheral,
                                LoopbackTransport& transport)
{
    /* The receiving end must be listening, or the bytes it would have accepted are
     * simply lost. */
    if (sink.discovery.subscriptions == 0) return;
    /*
     * An engine whose link has FAILED is not listening either: it clears its GATT
     * subscription and judges every later byte event STALE (-3). Continuing to spray
     * fragments at it would turn one real failure into a wall of identical refusals,
     * so the relay stops for this direction - and the bytes that consequently never
     * crossed are reported by the test's own byte-for-byte comparison, while the
     * failure itself is reported by its link-state assertion.
     */
    if (sink.snapshot().link_state == FLY_SESSION_LINK_FAILED_V2) return;
    /*
     * Once the QUIC channel is bound the engine releases the BLE/Bonjour bootstrap
     * (session_engine.cpp:4985-4986) and clears its own GATT subscription
     * (:2605), so every later DISCOVERY_BYTES event would be judged STALE. The
     * link knows the disconnect happened because the engine asked its own
     * discovery port for it, so nothing further is carried in that direction.
     */
    if (sink.discovery.disconnects > 0) return;

    if (state.fragments_relayed < source.discovery.written_fragments.size())
    {
        const auto direction = source_is_peripheral
            ? wire::GattPhysicalDirection::PeripheralToCentral
            : wire::GattPhysicalDirection::CentralToPeripheral;
        const std::uint64_t generation =
            source.discovery.write_token.connection_generation;
        wire::GattReassembler locator(generation, direction);
        std::size_t stop = source.discovery.written_fragments.size();
        for (std::size_t index = state.fragments_relayed;
             index < source.discovery.written_fragments.size(); ++index)
        {
            const auto& fragment = source.discovery.written_fragments[index];
            std::vector<std::uint8_t> logical;
            const auto result = locator.accept(generation, 0, fragment.data(),
                                               fragment.size(), &logical, nullptr);
            if (result == wire::GattFragmentResult::Complete)
            {
                stop = index + 1;
                break;
            }
            if (result != wire::GattFragmentResult::Accepted &&
                result != wire::GattFragmentResult::Duplicate)
            {
                check(false,
                      "the relay could not locate the end of the source engine's own "
                      "logical message inside its own written fragments");
                return;
            }
        }
        /*
         * W3 tamper matrix. The group of fragments that makes up ONE logical
         * message is COPIED only while a logical-message tamper is armed, so the
         * ordinary path still delivers the source's own fragment object without
         * a copy and without a chance of being altered.
         */
        /*
         * W3 tamper matrix. The armed rule is applied ONCE per logical message and
         * the RESULT is remembered in the direction's state, so a retry after
         * BACKPRESSURE re-offers the same tampered bytes instead of silently
         * falling back to the sender's originals.
         */
        const std::size_t group_begin = state.fragments_relayed;
        if (transport.logical_tamper_armed())
        {
            std::vector<std::vector<std::uint8_t>> group(
                source.discovery.written_fragments.begin() +
                    static_cast<std::ptrdiff_t>(group_begin),
                source.discovery.written_fragments.begin() +
                    static_cast<std::ptrdiff_t>(stop));
            if (transport.tamper_logical_group(
                    source_is_peripheral
                        ? LoopbackTransport::QuicFilter::PeripheralToCentral
                        : LoopbackTransport::QuicFilter::CentralToPeripheral,
                    &group))
                for (std::size_t slot = 0; slot < group.size(); ++slot)
                    state.remember_replacement(group_begin + slot,
                                               std::move(group[slot]));
        }
        int replacements_delivered = 0;
        for (std::size_t index = group_begin; index < stop; ++index)
        {
            /* `group_begin`, not `state.fragments_relayed`: the watermark advances
             * inside this loop, so subtracting it would re-deliver the first
             * fragment of the group forever. */
            const std::vector<std::uint8_t>* replacement =
                state.replacement_at(index);
            const std::vector<std::uint8_t>& fragment =
                replacement != nullptr
                    ? *replacement
                    : source.discovery.written_fragments[index];
            if (!relay_fragment_once(sink, state, fragment, &state.delivered))
                return;
            if (replacement != nullptr) ++replacements_delivered;
            ++state.fragments_relayed;
        }
        if (replacements_delivered > 0)
        {
            state.replaced_delivered += replacements_delivered;
            transport.note_tampered_group_delivered();
        }
    }

    if (state.acks_relayed < source.discovery.ack_fragments.size())
    {
        /* An engine's own acknowledgements go back to the peer whose fragments they
         * acknowledge, one fragment per call. Without them the writer of a logical
         * message stops half-way through it while its acknowledgement is
         * outstanding. */
        if (!relay_fragment_once(sink, state,
                                 source.discovery.ack_fragments[state.acks_relayed],
                                 &state.delivered_acks))
            return;
        ++state.acks_relayed;
    }
}

/* Both directions of the one link the transport connected. */
inline void relay_gatt(LoopbackTransport& transport, RelayReport& report)
{
    EngineFixture* peripheral = transport.peripheral_fixture();
    EngineFixture* central = transport.central_fixture();
    if (peripheral == nullptr || central == nullptr) return;
    relay_one_direction(*peripheral, report.peripheral_to_central, *central, true,
                        transport);
    relay_one_direction(*central, report.central_to_peripheral, *peripheral, false,
                        transport);
}

/*
 * ------------------------------------------------------------------------- *
 * Step 5: the byte-accurate QUIC stream loopback.
 *
 * WHAT CROSSES
 *   Only the source engine's OWN `write` bytes, verbatim and in order, plus the
 *   empty unit that carries the FIN the writer asked for. Nothing here
 *   re-frames, re-encodes or reorders anything: the receiving engine parses the
 *   sender's own encoder output with its own decoders, which is what makes the
 *   two engines two ends of one QUIC connection rather than two scripts.
 *
 * THE TOKEN
 *   Inbound bytes are accepted ONLY under the token of the read the receiving
 *   engine itself granted (session_engine.cpp:2432/:2436 select the scheduler by
 *   event token and parse_provider_event_v2 then compares that same token), so
 *   the relay uses the token recorded when `grant_read_credit` was called - never
 *   `last_token`, which later writes overwrite.
 *
 * THE EVENT FORM
 *   A class-2 QUIC_DATA event is BUFFER form with terminal = 0 (the contract in
 *   provider_events.cpp:86-88); a terminal 1 data event is a CONTRACT_VIOLATION,
 *   and the engine's Control reader is deliberately unjournaled for exactly this
 *   reason (link_handshake_scheduler.cpp:488-499).
 *
 * PACING AND BACKPRESSURE
 *   At most ONE unit per direction per round, and both engines are pumped to idle
 *   between units. The delivery watermark and the receiving inbox's event
 *   sequence advance ONLY on FLY_SESSION_V2_ACCEPTED; BACKPRESSURE (-7) means the
 *   same unit is attempted again after the receiver is pumped, and any other
 *   result is reported with its numeric code instead of being retried or hidden.
 *
 * A STREAM MUST BE READY TO RECEIVE
 *   A unit is delivered only when the receiving engine's next unconsumed granted
 *   read is for THAT stream. The write queue is FIFO, so a unit whose reader has
 *   not asked yet simply waits at the head; nothing is ever handed to a read that
 *   belongs to another stream.
 * ------------------------------------------------------------------------- */

struct QuicDirectionState;

/* One unit as a class-2 QUIC_DATA event. Returns the engine's raw result so the
 * caller can distinguish ACCEPTED, BACKPRESSURE and a real refusal. */
inline fly_session_result_v2 deliver_quic_data_event(
    EngineFixture& sink, const fly_session_op_token_v2& token,
    const std::vector<std::uint8_t>& bytes, std::uint64_t sequence)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{
        bytes.empty() ? nullptr : bytes.data(),
        static_cast<std::uint32_t>(bytes.size()), 0};
    check(fly_session_buffer_create_copy_v2(source, &buffer) == FLY_SESSION_V2_OK,
          "the relay owns an immutable copy of the stream bytes it carries");
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = sequence;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    /* terminal = 0: QUIC_DATA_V2's contract, and the reason the Control read is
     * not journaled. */
    event.terminal = 0;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = FLY_SESSION_PROVIDER_QUIC_DATA_V2;
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = buffer;
    payload.logical_size = bytes.size();
    payload.generation = token.connection_generation;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    const auto result = fly_session_deliver_v2(sink.quic.inbox, &event);
    fly_session_buffer_release_v2(buffer);
    return result;
}

/* The units a writer's own write log says must cross. Kept next to the relay so
 * the test's byte-for-byte proof is a comparison against the WRITER, not against
 * a second copy of the relay's own logic. */
struct QuicExpectedUnit final
{
    std::vector<std::uint8_t> bytes;
    fly_session_resource_handle_v2 stream = 0;
    std::uint32_t fin = 0;
};

inline std::vector<QuicExpectedUnit> expected_quic_units(
    const EngineFixture& fixture)
{
    std::vector<QuicExpectedUnit> units;
    for (const auto& write : fixture.quic.written_streams)
    {
        QuicExpectedUnit payload{};
        payload.bytes = write.bytes;
        payload.stream = write.stream;
        payload.fin = write.finish != 0 && write.bytes.empty() ? 1u : 0u;
        units.push_back(std::move(payload));
        if (write.finish != 0 && !write.bytes.empty())
        {
            QuicExpectedUnit fin{};
            fin.stream = write.stream;
            fin.fin = 1;
            units.push_back(std::move(fin));
        }
    }
    return units;
}

enum class QuicRelayOutcome : std::uint8_t
{
    /* Nothing was staged, so this direction had nothing to do. */
    Empty = 0,
    /* One unit was delivered and accepted. */
    Delivered = 1,
    /* The receiver applied backpressure: the SAME unit is retried later. */
    Backpressure = 2,
    /* A unit is staged but the receiver has not granted a read for its stream
     * yet (or the transport's filter is holding it): no byte was moved. */
    Waiting = 3
};

/*
 * Carries at most ONE unit from `source` to `sink`. The bytes are the source
 * engine's own; this function chooses nothing but the destination.
 */
inline QuicRelayOutcome relay_quic_one_direction(
    LoopbackTransport& transport, LoopbackTransport::QuicFilter direction,
    EngineFixture& source, QuicDirectionState& state, EngineFixture& sink)
{
    /* Stage the next own write, if the queue is empty. */
    if (state.pending.empty() &&
        state.writes_staged < source.quic.written_streams.size())
    {
        const auto& write = source.quic.written_streams[state.writes_staged++];
        QuicDirectionState::Unit unit{};
        unit.stream = write.stream;
        unit.bytes = write.bytes;
        unit.fin = write.finish != 0 && write.bytes.empty();
        /*
         * W3 tamper matrix. The unit holds the sender's OWN encoded bytes, still
         * undelivered, so this is the one place a byte-level fault can be
         * injected between "the sender encoded it" and "the receiver decodes it".
         * The armed rule names at most one unit and rewrites exactly one byte of
         * it; a read that is never granted leaves the unit tampered but unsent,
         * which `tampered_units()` reports.
         */
        if (!unit.bytes.empty() &&
            transport.targets_unit(direction, unit.stream, unit.bytes))
            transport.apply_tamper(&unit.bytes);
        state.pending.push_back(std::move(unit));
        if (write.finish != 0 && !write.bytes.empty())
        {
            QuicDirectionState::Unit fin{};
            fin.stream = write.stream;
            fin.fin = true;
            state.pending.push_back(std::move(fin));
        }
    }
    if (state.pending.empty()) return QuicRelayOutcome::Empty;

    QuicDirectionState::Unit& unit = state.pending.front();
    /* Match a granted read for THIS stream. Control and State Commit can both
     * have outstanding credit, so FIFO-by-grant-order would stall the second
     * stream behind a leftover Control read. */
    if (state.grants_consumed.size() < sink.quic.granted_reads.size())
        state.grants_consumed.resize(sink.quic.granted_reads.size(), 0);
    std::size_t found = sink.quic.granted_reads.size();
    for (std::size_t index = 0; index < sink.quic.granted_reads.size(); ++index)
    {
        if (state.grants_consumed[index] != 0) continue;
        if (sink.quic.granted_reads[index].stream != unit.stream) continue;
        found = index;
        break;
    }
    if (found == sink.quic.granted_reads.size())
        return QuicRelayOutcome::Waiting;
    const auto& read = sink.quic.granted_reads[found];
    if (unit.bytes.size() > read.credit)
    {
        check(false,
              "the loopback QUIC relay would deliver more stream bytes than the "
              "receiving engine granted credit for");
        return QuicRelayOutcome::Waiting;
    }
    if (transport.withholds(direction, unit.bytes))
    {
        if (!unit.held)
        {
            unit.held = true;
            ++state.held_units;
        }
        return QuicRelayOutcome::Waiting;
    }

    const auto result =
        deliver_quic_data_event(sink, read.token, unit.bytes, state.next_sequence);
    if (result == FLY_SESSION_V2_ACCEPTED)
    {
        ++state.next_sequence;
        ++state.reads_answered;
        state.grants_consumed[found] = 1;
        state.delivered.push_back(unit.bytes);
        state.delivered_streams.push_back(unit.stream);
        state.delivered_fin.push_back(unit.fin ? 1u : 0u);
        ++state.units;
        if (unit.fin) ++state.fins;
        state.pending.erase(state.pending.begin());
        return QuicRelayOutcome::Delivered;
    }
    if (result == FLY_SESSION_V2_BACKPRESSURE) return QuicRelayOutcome::Backpressure;
    char message[512];
    const auto snapshot = sink.snapshot();
    std::snprintf(message, sizeof(message),
                  "the receiving engine refused a loopback QUIC stream event with "
                  "result %d, which is neither ACCEPTED nor BACKPRESSURE (its link "
                  "state is %u, reason '%s')",
                  static_cast<int>(result),
                  static_cast<unsigned>(snapshot.link_state),
                  snapshot.primary_reason_key);
    check(false, message);
    return QuicRelayOutcome::Waiting;
}

struct QuicRelayRound final
{
    bool delivered = false;
    bool backpressured = false;
    int held = 0;
};

/* Both directions of the one QUIC connection the transport owns. */
inline QuicRelayRound relay_quic(LoopbackTransport& transport, RelayReport& report)
{
    QuicRelayRound round{};
    EngineFixture* peripheral = transport.peripheral_fixture();
    EngineFixture* central = transport.central_fixture();
    if (peripheral == nullptr || central == nullptr) return round;
    const auto outward = relay_quic_one_direction(
        transport, LoopbackTransport::QuicFilter::PeripheralToCentral,
        *peripheral, report.quic_peripheral_to_central, *central);
    const auto inward = relay_quic_one_direction(
        transport, LoopbackTransport::QuicFilter::CentralToPeripheral,
        *central, report.quic_central_to_peripheral, *peripheral);
    round.delivered = outward == QuicRelayOutcome::Delivered ||
                      inward == QuicRelayOutcome::Delivered;
    round.backpressured = outward == QuicRelayOutcome::Backpressure ||
                          inward == QuicRelayOutcome::Backpressure;
    round.held = report.quic_peripheral_to_central.held_units +
                 report.quic_central_to_peripheral.held_units;
    return round;
}

/*
 * The APP's part of this flow, and nothing more: when an engine publishes the SAS
 * confirmation the public ABI requires the app to answer, this submits it on that
 * engine through the same public action ABI a real app uses. It authors no transport
 * event and no peer message - the engine still decides whether the action applies.
 *
 * `observed` (optional) collects every action kind the engines publish while this
 * flow runs, so a test can state which actions the path really offers. Exactly one
 * of them is ever submitted: CONFIRM_SAS, the app's own decision. No other kind is
 * answered, because this harness does not understand the semantics of one it has
 * never seen and will not invent a response for it.
 */
inline bool confirm_pairing_sas_when_the_abi_asks(
    EngineFixture& fixture, std::uint64_t request_id,
    std::vector<std::uint32_t>* observed = nullptr)
{
    std::vector<fly_session_action_descriptor_v2> actions;
    fixture.snapshot(&actions);
    if (observed != nullptr)
    {
        for (const auto& action : actions)
            if (std::find(observed->begin(), observed->end(),
                          action.action_kind) == observed->end())
                observed->push_back(action.action_kind);
    }
    const auto* confirm = find_action(actions, FLY_SESSION_ACTION_CONFIRM_SAS_V2);
    const bool submit_it = confirm != nullptr && confirm->enabled;
    if (submit_it) submit(fixture, *confirm, request_id, false);
    for (auto& action : actions)
        fly_session_approval_token_release_v2(action.approval_token);
    return submit_it;
}

/*
 * Says whether this engine currently offers the SAS confirmation, i.e. whether
 * the APP still owes its own decision on this end. It only inspects the published
 * actions - it authors no event and submits nothing - and it releases the approval
 * tokens it looked at, exactly like the submitter below.
 */
inline bool pairing_sas_is_offered(EngineFixture& fixture,
                                   std::vector<std::uint32_t>* observed = nullptr)
{
    std::vector<fly_session_action_descriptor_v2> actions;
    fixture.snapshot(&actions);
    if (observed != nullptr)
    {
        for (const auto& action : actions)
            if (std::find(observed->begin(), observed->end(),
                          action.action_kind) == observed->end())
                observed->push_back(action.action_kind);
    }
    const auto* confirm = find_action(actions, FLY_SESSION_ACTION_CONFIRM_SAS_V2);
    const bool offered = confirm != nullptr && confirm->enabled;
    for (auto& action : actions)
        fly_session_approval_token_release_v2(action.approval_token);
    return offered;
}

/*
 * The two-engine driver: alternate "carry the peer's own bytes" with "answer this
 * engine's provider work" until neither bytes moved nor provider work was answered
 * for `idle_rounds_required` consecutive rounds.
 *
 * `first` and `second` are the pump order; WHICH END IS WHICH comes from the
 * transport, never from the caller. Exceeding `max_rounds` is a FAILURE, never a
 * hang: an exchange that will not settle is a defect the test has to report.
 *
 * `answer_the_app_action` is opt-in because the SAS confirmation is the app's own
 * decision: submitting it walks the two engines out of the pair exchange into the
 * capability/bearer/QUIC stages. Increments that stop at the pair exchange leave
 * it off, and the confirmation stays visible in the engine's own projections; the
 * QUIC loopback (step 5) turns it on, because the engines cannot reach the QUIC
 * stage without it. When it is on, the app answers on BOTH ends together - the two
 * humans compare the SAS and confirm as a pair - which is a pacing choice about
 * the app, not about the protocol.
 *
 * KNOWN ENGINE BLOCKER ON THIS PATH (reported, not worked around)
 *   The committed engine hands ONE operation id to two different operations once
 *   both ends confirm: `PairKeyConfirmScheduler` mints an AEAD operation from
 *   `next_operation_id_` inside the GATT KeyConfirm handler
 *   (session_engine.cpp:2058 -> accept_peer_envelope -> prepare_aead), and that
 *   scheduler's ids are only folded back into `next_operation_id_` when one of its
 *   COMPLETIONS is processed (:4777). A physical-ack or fragment write dispatched
 *   in the same worker iteration mints its own token from the same stale counter
 *   (`make_link_operation_token_locked`, :364 and :2773), so a crypto open and a
 *   discovery write end up sharing one operation id. The engine's own
 *   completion-record dedup keys on (operation_id, event_sequence) only (:2387-2399)
 *   and then refuses the second completion with CONTRACT_VIOLATION (-15). The
 *   harness reports that as an engine-side operation-id defect (see
 *   report_provider_requests in the e2e test) instead of hiding it.
 */
inline void relay_and_pump_until_idle(LoopbackTransport& transport,
                                      EngineFixture& first, PumpState& first_pump,
                                      EngineFixture& second, PumpState& second_pump,
                                      PumpLimits limits, int max_rounds,
                                      int idle_rounds_required,
                                      RelayReport* report = nullptr,
                                      bool answer_the_app_action = false)
{
    RelayReport local{};
    if (report == nullptr) report = &local;
    check((transport.peripheral_fixture() == &first ||
           transport.peripheral_fixture() == &second) &&
              (transport.central_fixture() == &first ||
               transport.central_fixture() == &second),
          "the driver pumps exactly the two ends the transport connected");

    int idle_rounds = 0;
    for (int round = 0; round < max_rounds; ++round)
    {
        ++report->rounds;
        const std::size_t fragments_before =
            report->peripheral_to_central.delivered.size() +
            report->peripheral_to_central.delivered_acks.size() +
            report->central_to_peripheral.delivered.size() +
            report->central_to_peripheral.delivered_acks.size();
        const int quic_units_before = report->quic_peripheral_to_central.units +
                                      report->quic_central_to_peripheral.units;
        const int answers_before =
            first_pump.counts.answers + second_pump.counts.answers;

        relay_gatt(transport, *report);
        const auto quic = relay_quic(transport, *report);
        pump_engine(first, first_pump, limits);
        pump_engine(second, second_pump, limits);

        const std::size_t fragments_after =
            report->peripheral_to_central.delivered.size() +
            report->peripheral_to_central.delivered_acks.size() +
            report->central_to_peripheral.delivered.size() +
            report->central_to_peripheral.delivered_acks.size();
        const int quic_units_after = report->quic_peripheral_to_central.units +
                                     report->quic_central_to_peripheral.units;
        const int answers_after =
            first_pump.counts.answers + second_pump.counts.answers;

        /*
         * A unit the transport's filter is holding is deliberately NOT progress:
         * it never reached the `delivered` list, so the unit counts below already
         * see the stall, and the exchange settles exactly as the withheld message
         * says. A BACKPRESSURE retry IS progress in the sense that matters here -
         * the receiver has to be pumped before the same unit can be offered again
         * - so it prevents a premature idle, while a peer that refuses forever is
         * still reported by the round bound as a failure rather than hanging.
         */
        const bool progressed =
            fragments_after != fragments_before ||
            quic_units_after != quic_units_before ||
            answers_after != answers_before || quic.backpressured;
        if (progressed) idle_rounds = 0; else ++idle_rounds;

        bool app_acted = false;
        if (answer_the_app_action)
        {
            /* The two humans compare the SAS and confirm together, so the app
             * answers on BOTH ends at once - a pacing choice about the app, not
             * about the protocol. See the header comment for the engine-side
             * operation-id defect this path runs into afterwards. */
            const bool first_ready =
                pairing_sas_is_offered(first, &report->app_action_kinds);
            const bool second_ready =
                pairing_sas_is_offered(second, &report->app_action_kinds);
            if (first_ready && second_ready)
            {
                const bool first_acted =
                    confirm_pairing_sas_when_the_abi_asks(first, 700 + round);
                const bool second_acted =
                    confirm_pairing_sas_when_the_abi_asks(second, 800 + round);
                app_acted = first_acted || second_acted;
                if (app_acted) ++report->app_actions;
            }
        }
        /* The app's own decision is progress: the engines have something new to
         * do, so this round cannot be an idle one. */
        if (app_acted) idle_rounds = 0;

        if (idle_rounds >= idle_rounds_required) return;
    }
    check(false,
          "the two-engine driver did not settle within max_rounds: the engines kept "
          "relaying bytes or asking for provider work without ever becoming idle");
}

/*
 * Shuts one engine down and asserts that it really destroyed.
 *
 * A provider that cancels synchronously says so once, here: `cancel` returning
 * ACCEPTED would tell the engine to wait for a cancellation terminal this provider
 * would never send, and shutdown would then be waiting on the harness rather than
 * on the engine. That is the one provider capability this helper declares; every
 * terminal it does send still has to be earned by a real request, because the pump
 * is driven by the ports' own counters.
 *
 * Destroying with `destroy == OK` is asserted rather than ignored: an engine that
 * will not release its resources is a defect, not a teardown detail.
 */
inline void shutdown_engine_with_the_pump(EngineFixture& fixture,
                                         PumpState& state,
                                         PumpLimits limits = {})
{
    fixture.bearer.cancel_result = FLY_SESSION_V2_OK;
    fly_session_begin_shutdown_v2(fixture.engine, 900);
    fixture.executor.run_all();
    pump_engine(fixture, state, limits);
    fixture.executor.run_all();
    check(fly_session_destroy_v2(fixture.engine) == FLY_SESSION_V2_OK,
          "the engine destroys after the pump answered its provider work");
    fixture.engine = nullptr;
}

} // namespace flynes::session::loopback

#endif
