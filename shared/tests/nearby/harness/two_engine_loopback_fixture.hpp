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
inline fly_session_result_v2 read_clock(void*, fly_session_clock_sample_v2* out)
{
    if (!out) return FLY_SESSION_V2_INVALID_ARGUMENT;
    out->continuous_ns = 1;
    out->suspend_inclusive = 1;
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
        std::vector<std::uint8_t> last_domain;
        std::array<std::uint8_t, 32> last_digest{};
        fly_session_op_token_v2 last_token{};
        fly_session_inbox_v2_t* inbox = nullptr;
        /* Step 2: when a world is attached, key material comes from it and the
         * terminal is queued immediately. The point index is derived from the
         * purpose and the side so that every key in the pair is a *different*
         * on-curve point — the wire codecs require e.g. the long-term identity key
         * and the session signing key to differ. */
        LoopbackWorld* world = nullptr;
        LoopbackSide side = LoopbackSide::Initiator;

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
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            if (self->world != nullptr)
            {
                const auto handle = self->world->allocate_point(
                    point_index_for(purpose, self->side));
                loopback_deliver_resource(inbox, *token,
                                          FLY_SESSION_PROVIDER_KEY_HANDLE_V2,
                                          handle);
            }
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
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            if (self->world != nullptr)
            {
                const auto* point = self->world->point_of(resource);
                if (point == nullptr) return FLY_SESSION_V2_INVALID_ARGUMENT;
                loopback_deliver_buffer(inbox, *token,
                                        FLY_SESSION_PROVIDER_KEY_PUBLIC_V2,
                                        point->data(), point->size());
            }
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
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            if (self->world != nullptr)
            {
                const auto* mine = self->world->point_of(resource);
                if (mine == nullptr) return FLY_SESSION_V2_INVALID_ARGUMENT;
                LoopbackPoint theirs{};
                std::copy_n(peer.data, theirs.size(), theirs.begin());
                /* ECDH STAND-IN, NOT ECDH: see LoopbackWorld::agree. Both sides
                 * derive the same secret, which is what the protocol layer needs;
                 * no NIST P-256 key-agreement property is exercised here. */
                const auto secret = self->world->agree(*mine, theirs);
                const auto handle = self->world->allocate_secret(secret);
                loopback_deliver_resource(
                    inbox, *token, FLY_SESSION_PROVIDER_KEY_AGREEMENT_V2, handle);
            }
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
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
            if (self->world != nullptr)
            {
                const auto* point = self->world->point_of(resource);
                if (point == nullptr) return FLY_SESSION_V2_INVALID_ARGUMENT;
                const auto signature = self->world->sign(
                    *point, domain.data, domain.size, digest);
                loopback_deliver_buffer(
                    inbox, *token, FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2,
                    signature.data(), signature.size());
            }
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
        /* Step 2: when a world is attached, every operation below is performed by
         * the shared deterministic world and its terminal is queued immediately,
         * instead of the test having to script an answer. */
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
            self->last_token = *token;
            self->capture(inbox);
            if (self->world != nullptr)
            {
                const auto bytes = self->world->random(
                    self->side, size, purpose.data, purpose.size);
                loopback_deliver_buffer(inbox, *token,
                                        FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2,
                                        bytes.data(), bytes.size());
            }
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
            self->last_salt.assign(salt.data, salt.data + salt.size);
            self->last_info.assign(info.data, info.data + info.size);
            self->capture(inbox);
            if (self->world != nullptr)
            {
                const auto* material = self->world->secret_of(secret);
                if (material == nullptr)
                    return FLY_SESSION_V2_INVALID_ARGUMENT;
                const auto derived =
                    self->world->hkdf(*material, salt.data, salt.size, info.data,
                                      info.size, size);
                const auto handle = self->world->allocate_secret(derived);
                loopback_deliver_resource(
                    inbox, *token, FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, handle);
            }
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
            self->capture(inbox);
            if (self->world != nullptr)
            {
                /* The mock verifier really recomputes the signature instead of
                 * accepting anything, so a tampered signature is still caught. */
                LoopbackPoint key{};
                std::copy_n(public_key.data, key.size(), key.begin());
                const bool ok = self->world->verify(
                    key, domain.data, domain.size, digest, signature.data);
                loopback_deliver_verification(
                    inbox, *token,
                    ok ? FLY_SESSION_V2_OK : FLY_SESSION_V2_AUTH_FAILED);
            }
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
            self->capture(inbox);
            if (self->world != nullptr)
            {
                const auto* material = self->world->secret_of(key);
                if (material == nullptr)
                    return FLY_SESSION_V2_INVALID_ARGUMENT;
                const auto tag =
                    self->world->hmac(*material, input.data, input.size);
                loopback_deliver_buffer(inbox, *token,
                                        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
                                        tag.data(), tag.size());
            }
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
            self->capture(inbox);
            if (self->world != nullptr)
            {
                const auto* material = self->world->secret_of(key);
                if (material == nullptr)
                    return FLY_SESSION_V2_INVALID_ARGUMENT;
                if (sealing)
                {
                    const auto sealed = self->world->seal(
                        *material, nonce.data, nonce.size, aad.data, aad.size,
                        input.data, input.size);
                    loopback_deliver_buffer(
                        inbox, *token, FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
                        sealed.data(), sealed.size());
                }
                else
                {
                    std::vector<std::uint8_t> opened;
                    const bool ok = self->world->open(
                        *material, nonce.data, nonce.size, aad.data, aad.size,
                        input.data, input.size, &opened);
                    if (!ok)
                        loopback_deliver_end(
                            inbox, *token,
                            FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
                            FLY_SESSION_V2_AUTH_FAILED);
                    else
                        loopback_deliver_buffer(
                            inbox, *token, FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
                            opened.data(), opened.size());
                }
            }
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
        std::vector<std::vector<std::uint8_t>> written_fragments;
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
            if (bytes.size() >= 2 && bytes[1] == static_cast<std::uint8_t>(
                    flynes::session::wire::GattLogicalType::PhysicalAck))
            {
                ++self->physical_acks;
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
        int writes = 0;
        int reads = 0;
        int cancels = 0;
        fly_session_op_token_v2 last_token{};
        fly_session_resource_handle_v2 last_resource = 0;
        std::uint64_t last_credit = 0;
        std::uint32_t last_finish = 0;
        std::vector<std::uint8_t> last_endpoint;
        std::vector<std::uint8_t> last_label;
        std::vector<std::uint8_t> last_context;
        std::vector<std::uint8_t> last_write;
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
            if (stream_kind != 1) return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->opened_bidi;
            self->capture(token, connection, inbox);
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 accept_bidi(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 connection, std::uint32_t,
            std::uint32_t stream_kind, fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Quic*>(context);
            if (stream_kind != 1) return FLY_SESSION_V2_INVALID_ARGUMENT;
            ++self->accepted_bidi;
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
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 cancel(
            void* context, const fly_session_op_token_v2*)
        {
            ++static_cast<Quic*>(context)->cancels;
            return FLY_SESSION_V2_OK;
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

    DeterministicExecutor executor;
    Platform platform;
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
    fly_session_ports_v2 ports{};
    fly_session_v2_t* engine = nullptr;

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
                  bool secure_pairing_ports = true)
        : EngineFixture(&shared_world, shared_side, secure_pairing_ports)
    {
    }

private:
    EngineFixture(LoopbackWorld* shared_world, LoopbackSide shared_side,
                  bool secure_pairing_ports)
    {
        key.world = shared_world;
        key.side = shared_side;
        crypto.world = shared_world;
        crypto.side = shared_side;
        clock.struct_size = FLY_SESSION_CLOCK_PORT_V2_SIZE;
        clock.abi_version = FLY_SESSION_ABI_VERSION_2;
        clock.retain = retain_noop;
        clock.release = release_noop;
        clock.read_continuous = read_clock;
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
        quic_port.close = unavailable_quic_close;
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
    check(fly_session_deliver_v2(inbox, &event) == FLY_SESSION_V2_ACCEPTED,
          "typed provider buffer completion enters public engine");
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
    check(fly_session_deliver_v2(inbox, &event) == FLY_SESSION_V2_ACCEPTED,
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
        event.token = fixture.discovery.last_token;
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


} // namespace flynes::session::loopback

#endif
