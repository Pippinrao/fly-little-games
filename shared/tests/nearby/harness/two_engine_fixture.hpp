#ifndef FLYNES_TESTS_NEARBY_HARNESS_TWO_ENGINE_FIXTURE_HPP
#define FLYNES_TESTS_NEARBY_HARNESS_TWO_ENGINE_FIXTURE_HPP

/*
 * W3 harness: two *public* engines driven against each other over provider
 * boundaries only.
 *
 * Provenance. The provider scaffolding in the generated region below is carved
 * line-for-line out of the frozen reference integration test
 *   shared/tests/nearby/integration/test_two_engine_empty_lobby.cpp
 * (see out/tools/assemble_two_engine_fixture.ps1) because the W1/W2/W3
 * conventions forbid inventing a second, differently-worded set of provider
 * fakes for the same seams.
 *
 * What this harness may and may not do. It creates two independent engines
 * through the public ABI (`fly_session_create_v2`) and moves bytes between them
 * only through provider surfaces: encoded GATT fragments, provider completion
 * events and the QUIC port. It never calls the other engine's reducer, never
 * injects `authenticated = true` and never fabricates verified pair evidence.
 *
 * Integration state (2026-09-15, baseline e8703e4 + W0 QUIC wiring e22cbff).
 * LINK_HELLO / LINK_READY are frozen as contracts (link_control_contract.hpp)
 * but are NOT yet wired into SessionEngine: that is W0's Task 9. Until then two
 * public engines can reach FLY_SESSION_LINK_CONNECTING_V2 but must never publish
 * FLY_SESSION_LINK_CONNECTED_LOBBY_V2. Every assertion that only becomes
 * reachable after that wiring is gated on `kHelloReadyWiredIntoEngineV1` and is
 * listed next to the switch in the scenario files. Do not flip it to make a
 * test green.
 */

#include <flynes/flynes_session.h>

#include "../harness/deterministic_executor.hpp"

#include "wire/gatt_fragment.hpp"
#include "wire/sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <vector>

namespace flynes::tests::nearby {

/* ------------------------------------------------------------------ switch */

/*
 * Flips to true only when W0's Task 9 has wired LINK_HELLO / LINK_READY into
 * SessionEngine. While it is false the scenarios assert the strongest *real*
 * reachable outcome instead: never CONNECTED_LOBBY.
 */
inline constexpr bool kHelloReadyWiredIntoEngineV1 = false;

/*
 * A frame that has been produced by one engine's providers and is on its way to
 * the other engine's inbox. It is an *encoded* provider payload: the scheduler
 * never sees a decoded session message, only the exact event the provider layer
 * would have delivered.
 */
struct TransportFrameV1;

/* ------------------------------------------------------- shared test idiom */

extern int g_failures;
void check(bool value, const char* message);

/* ------------------------------------------------------- resource oracle */

/*
 * Balanced-resource oracle.
 *
 * Every counted class is paired: acquire/release (buffers, secrets, materials,
 * streams, paths, inboxes) or open/close (pending operations, engine contexts).
 * A run that ends with a non-zero total has leaked something; the stale-token
 * counters prove that a late completion for a dead operation released *old*
 * resources rather than new ones.
 */
struct ResourceLedgerV1 final
{
    struct CounterV1
    {
        int acquired = 0;
        int released = 0;

        void acquire() noexcept { ++acquired; }
        void release() noexcept { ++released; }
        int outstanding() const noexcept { return acquired - released; }
    };

    CounterV1 buffer_retains{};   /* fly_session_buffer_retain_v2 */
    CounterV1 key_handles{};      /* key port generate / release_key */
    CounterV1 secret_handles{};   /* crypto secrets / release_secret */
    CounterV1 material_handles{}; /* TLS material / release_material */
    CounterV1 stream_handles{};   /* QUIC streams and their FIN/RESET */
    CounterV1 path_handles{};     /* QUIC connections, listeners, paths */
    CounterV1 credentials{};      /* bearer credentials */
    CounterV1 inboxes{};          /* fly_session_inbox_retain_v2/release */
    CounterV1 pending_operations{};
    CounterV1 engine_contexts{};  /* create/destroy */

    CounterV1 completed_operations{};      /* every terminal completion */
    CounterV1 stale_completions{};         /* completions rejected as stale */
    CounterV1 reclaimed_stale_resources{}; /* resources freed by those */

    std::vector<std::uint64_t> stale_operation_ids{};

    int total_outstanding() const noexcept
    {
        return buffer_retains.outstanding() + key_handles.outstanding() +
               secret_handles.outstanding() + material_handles.outstanding() +
               stream_handles.outstanding() + path_handles.outstanding() +
               credentials.outstanding() + inboxes.outstanding() +
               pending_operations.outstanding() +
               engine_contexts.outstanding();
    }

    void note_stale_completion(std::uint64_t operation_id) noexcept
    {
        stale_completions.acquire();
        stale_operation_ids.push_back(operation_id);
    }
};

/* ------------------------------------------------------- wire evidence */

struct SignatureEvidenceV1 final
{
    fly_session_op_token_v2 token{};
    std::uint32_t purpose = 0;
    std::vector<std::uint8_t> domain;
    std::array<std::uint8_t, 32> digest{};
    std::uint32_t encoding = 0;
    fly_session_resource_handle_v2 resource = 0;
};

/*
 * Counters that only move when bytes genuinely cross a provider boundary. They
 * are deliberately *not* settable from a scenario: the only way to move them is
 * to have the engine call the provider port.
 */
struct WireCountersV1 final
{
    std::uint64_t gatt_fragments_out = 0;
    std::uint64_t gatt_logical_bytes = 0;
    std::uint64_t quic_stream_bytes_out = 0;
    std::uint64_t quic_writes = 0;
    std::uint64_t quic_opens = 0;
    std::uint64_t inbox_events_delivered = 0;
    std::uint64_t inbox_events_accepted = 0;
    std::uint64_t wire_seals = 0;
    std::uint64_t wire_opens = 0;

    std::uint64_t total() const noexcept
    {
        return gatt_fragments_out + gatt_logical_bytes + quic_stream_bytes_out +
               quic_writes + quic_opens + inbox_events_delivered +
               inbox_events_accepted + wire_seals + wire_opens;
    }
};

/* ------------------------------------------------------- engine fixture */

/*
 * One public engine plus its own provider contexts and durable stores.
 *
 * Everything from here to the END GENERATED CARVE marker is carved
 * line-for-line out of the reference integration test by
 * out/tools/assemble_two_engine_fixture.ps1. Do not hand-edit that region:
 * change the carve list in the script and re-run
 * out/tools/build_two_engine_fixture_hpp.ps1.
 *
 * The definition lives in the header because the scenarios construct the
 * fixture directly and read snapshots from it.
 */


// W3 harness: reference provider scaffolding, carved line-for-line out of
//   shared/tests/nearby/integration/test_two_engine_empty_lobby.cpp
// which stays read-only and is owned by W0. Generating this from the single
// reference is deliberate: the conventions forbid inventing a second,
// differently-worded set of provider fakes for the same seams.
//
// W3 delta against the reference (the ONLY intentional differences):
//   * read_clock reads the owning engine's own clock instead of a constant,
//     so the deterministic scheduler can advance one endpoint on its own;
//   * the QUIC port is swappable, so the loopback QUIC fixture can take over
//     the transport without touching the engine, ports or the public ABI.

/* Foreground declaration: the real definition lives in the harness header, which
 * includes this file. EngineFixture only holds a pointer to it. */
struct ResourceLedgerV1;

// ---------------------------------------------------------------------------
// carved: retain/release no-ops
// ---------------------------------------------------------------------------
inline void retain_noop(void*) {}
inline void release_noop(void*) {}

// ---------------------------------------------------------------------------
// carved: platform state port
// ---------------------------------------------------------------------------
struct Platform final
{
    std::uint64_t clock_ns = 1;
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

// ---------------------------------------------------------------------------
// carved: deterministic platform clock (W3: per engine)
// ---------------------------------------------------------------------------
inline fly_session_result_v2 read_clock(void* context, fly_session_clock_sample_v2* out)
{
    if (!out) return FLY_SESSION_V2_INVALID_ARGUMENT;
    const auto* owner = static_cast<const Platform*>(context);
    out->continuous_ns = owner != nullptr ? owner->clock_ns : 1;
    out->suspend_inclusive = 1;
    out->boot_generation[0] = 1;
    return FLY_SESSION_V2_OK;
}

// ---------------------------------------------------------------------------
// carved: unavailable (missing) provider operations
// ---------------------------------------------------------------------------
inline fly_session_result_v2 unavailable_cancel(
    void*, const fly_session_op_token_v2*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_key_generate(
    void*, const fly_session_op_token_v2*, std::uint32_t,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_key_open(
    void*, const fly_session_op_token_v2*, std::uint32_t,
    fly_session_bytes_v2, fly_session_bytes_v2, const std::uint8_t[32],
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_key_public(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_key_sign(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, fly_session_bytes_v2, const std::uint8_t[32],
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_key_agree(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_key_release(
    void*, fly_session_resource_handle_v2)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_key_destroy(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2,
    std::uint64_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_crypto_random(
    void*, const fly_session_op_token_v2*, std::uint32_t,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_crypto_hkdf(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, std::uint32_t,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_crypto_aead(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, fly_session_bytes_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_crypto_verify(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2,
    fly_session_bytes_v2, const std::uint8_t[32], fly_session_bytes_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_crypto_hmac(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_secret_release(
    void*, fly_session_resource_handle_v2)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_tls_create(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_tls_restore(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2,
    const std::uint8_t[32], fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_tls_release(
    void*, fly_session_resource_handle_v2)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_bearer_start(
    void*, const fly_session_op_token_v2*, const std::uint8_t[32],
    fly_session_resource_handle_v2, std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_bearer_resolve(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_bearer_release(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_prepare_credential(
    void*, const fly_session_op_token_v2*, const std::uint8_t[32],
    fly_session_bytes_v2, fly_session_resource_handle_v2, std::uint32_t,
    fly_session_bytes_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_quic_start(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_resource_handle_v2,
    const fly_session_quic_connect_policy_v2*, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_quic_inspect(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_quic_exporter(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, std::uint32_t,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_quic_stream(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_quic_write(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_buffer_v2_t*, std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_quic_control(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint64_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_quic_datagram(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_resource_handle_v2, std::uint64_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_quic_query(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
inline fly_session_result_v2 unavailable_quic_close(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }

// ---------------------------------------------------------------------------
// carved: engine fixture and typed provider stubs
// ---------------------------------------------------------------------------
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
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 sign(
            void* context, const fly_session_op_token_v2* token,
            fly_session_resource_handle_v2 resource, std::uint32_t purpose,
            fly_session_bytes_v2 domain, const std::uint8_t digest[32],
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Key*>(context);
            if (resource == 0 || purpose != FLY_SESSION_KEY_DEVICE_IDENTITY_V2 ||
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
            return FLY_SESSION_V2_ACCEPTED;
        }

        static fly_session_result_v2 release_key(
            void* context, fly_session_resource_handle_v2)
        {
            ++static_cast<Key*>(context)->releases;
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
        ~Crypto() { fly_session_inbox_release_v2(inbox); }

        static fly_session_result_v2 random(
            void* context, const fly_session_op_token_v2* token,
            std::uint32_t size, fly_session_bytes_v2,
            fly_session_inbox_v2_t* inbox)
        {
            auto* self = static_cast<Crypto*>(context);
            ++self->randoms;
            self->last_size = size;
            self->last_token = *token;
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
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
            ++static_cast<Crypto*>(context)->releases;
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
        /* Size of the last encoded fragment the engine handed to this port. */
        std::size_t last_encoded_port_bytes = 0;
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
                self->last_encoded_port_bytes = bytes.size();
                return FLY_SESSION_V2_OK;
            }
            else
            {
                ++self->writes;
                self->last_encoded_port_bytes = bytes.size();
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
        std::array<std::uint8_t, 32> last_hash{};
        std::vector<std::uint8_t> last_value;
        fly_session_op_token_v2 last_token{};
        fly_session_inbox_v2_t* inbox = nullptr;
        ~ObjectStore() { fly_session_inbox_release_v2(inbox); }

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
            std::copy_n(expected_hash, 32, self->last_hash.begin());
            self->last_token = *token;
            fly_session_inbox_retain_v2(inbox);
            fly_session_inbox_release_v2(self->inbox);
            self->inbox = inbox;
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
    const fly_session_quic_port_v2* quic_port_override = nullptr;
    std::uint64_t engine_ordinal = 1;
    ResourceLedgerV1* ledger = nullptr;

    /* True while this engine still owns its public handle. */
    bool engine_is_live() const noexcept { return engine != nullptr; }

    /* How many encoded GATT fragments the harness has already relayed. */
    std::size_t discovery_fragments_captured = 0;

    /*
     * Record one typed provider completion this engine delivered into its own
     * public inbox. Called from the harness delivery helpers only, i.e. from
     * executor tasks replaying real provider callbacks.
     */
    void note_delivered_completion(bool accepted) noexcept
    {
        if (ledger != nullptr)
            ledger->completed_operations.acquire();
        if (wire != nullptr)
        {
            ++wire->inbox_events_delivered;
            if (accepted)
                ++wire->inbox_events_accepted;
        }
    }
    WireCountersV1* wire = nullptr;
    fly_session_ports_v2 ports{};
    fly_session_v2_t* engine = nullptr;

    explicit EngineFixture(bool secure_pairing_ports = true,
                           std::uint64_t local_engine_ordinal = 1,
                           ResourceLedgerV1* resource_ledger = nullptr,
                           WireCountersV1* wire_counters = nullptr)
    {
        engine_ordinal = local_engine_ordinal;
        ledger = resource_ledger;
        wire = wire_counters;
        clock.struct_size = FLY_SESSION_CLOCK_PORT_V2_SIZE;
        clock.abi_version = FLY_SESSION_ABI_VERSION_2;
        clock.context = &platform;
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
            ports.quic = quic_port_override != nullptr ? quic_port_override
                                                       : &quic_port;
            ports.object_store = &object_store_port;
        }
        fly_session_config_v2 config{};
        config.struct_size = FLY_SESSION_CONFIG_V2_SIZE;
        config.abi_version = FLY_SESSION_ABI_VERSION_2;
        config.action_queue_capacity = 8;
        config.notice_queue_capacity = 8;
        check(fly_session_create_v2(&config, &ports, &engine) == FLY_SESSION_V2_OK,
              "public engine creates");
        if (ledger != nullptr && engine != nullptr)
            ledger->engine_contexts.acquire();
    }

    ~EngineFixture()
    {
        if (engine)
        {
            fly_session_begin_shutdown_v2(engine, 900);
            executor.run_all();
            check(fly_session_destroy_v2(engine) == FLY_SESSION_V2_OK,
                  "public engine destroys after shutdown");
            if (ledger != nullptr)
                ledger->engine_contexts.release();
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
        if (fly_session_view_read_v2(view, &value) != FLY_SESSION_V2_OK)
        {
            /* The engine has no view yet (for example before its first
             * platform snapshot). Report the zero value rather than
             * dereferencing a view we do not hold. */
            fly_session_view_release_v2(view);
            return value;
        }
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

// ---------------------------------------------------------------------------
// carved: action lookup
// ---------------------------------------------------------------------------
inline const fly_session_action_descriptor_v2* find_action(
    const std::vector<fly_session_action_descriptor_v2>& actions,
    std::uint32_t kind)
{
    for (const auto& action : actions)
        if (action.action_kind == kind) return &action;
    return nullptr;
}

// ---------------------------------------------------------------------------
// carved: logical message helpers
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// carved: fixed P-256 points
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// carved: typed provider completion delivery
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// carved: authorization and probe plumbing
// ---------------------------------------------------------------------------
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

inline void start_responder_probe(EngineFixture& fixture, std::uint64_t request_id, bool deliver_platform_ready = true)
{
    if (deliver_platform_ready)
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


/* =========================================================================
 * END GENERATED CARVE
 * ========================================================================= */

/* ------------------------------------------------------- scheduler */

struct TransportFrameV1 final
{
    /* Which endpoint owns the inbox this frame must be delivered to. */
    std::uint64_t target_ordinal = 0;
    fly_session_inbox_v2_t* inbox = nullptr;
    fly_session_port_event_v2 event{};
    std::vector<std::uint8_t> payload{};
    std::uint64_t arrival_ns = 0;
    std::uint64_t sequence = 0;
    const char* origin = "";

    /*
     * Point event.payload at this frame's own copy of the bytes, so a reordered
     * or duplicated frame can never dangle into another frame's storage.
     */
    void rebind_payload() noexcept;

    ~TransportFrameV1();
    TransportFrameV1() = default;
    TransportFrameV1(const TransportFrameV1&);
    TransportFrameV1& operator=(const TransportFrameV1&);
    TransportFrameV1(TransportFrameV1&&) noexcept;
    TransportFrameV1& operator=(TransportFrameV1&&) noexcept;
};

/* ------------------------------------------------------- scheduler */

enum class InterruptionKindV1 : std::uint8_t
{
    Drop = 0,
    Duplicate = 1,
    ReorderToBack = 2,
    Cancel = 3
};

struct InterruptionV1 final
{
    InterruptionKindV1 kind = InterruptionKindV1::Drop;
    std::uint64_t at_index = 0;
};

struct ScheduleReportV1 final
{
    std::uint64_t delivered = 0;
    std::uint64_t dropped = 0;
    std::uint64_t duplicated = 0;
    std::uint64_t reordered = 0;
    std::uint64_t cancelled = 0;
    std::uint64_t late = 0;
    std::uint64_t refused = 0;
};

/*
 * Deterministic event scheduler.
 *
 * Every queued frame keeps its own token, scope, generation and payload kind
 * exactly as the provider produced them, so reordering/duplication/late
 * delivery exercises the engine's real fencing instead of a fixture shortcut.
 * Each endpoint has its own clock and its own paused flag: advancing one
 * endpoint's clock must not advance the other's.
 */
class DeterministicSchedulerV1 final
{
public:
    DeterministicSchedulerV1(EngineFixture* left, EngineFixture* right);

    /* Queue an encoded frame for delivery to `target_ordinal`. */
    void push(TransportFrameV1 frame);

    /* Deliver exactly one queued frame, in queue order. Returns false if empty. */
    bool run_next();

    /* Deliver everything currently queued, honouring the pause flags. */
    std::size_t run_until_idle();

    /* Apply a fault to the pending queue (repeatable, order-preserving). */
    void apply(InterruptionV1 interruption);

    void pause(std::uint64_t ordinal);
    void resume(std::uint64_t ordinal);
    bool paused(std::uint64_t ordinal) const noexcept;

    void advance_clock(std::uint64_t ordinal, std::uint64_t delta_ns);
    std::uint64_t clock_now(std::uint64_t ordinal) const noexcept;

    /* Destroy and recreate one engine's process, replaying its durable stores. */
    void restart(std::uint64_t ordinal);

    /* Discard every queued frame without delivering it. */
    void release_queue();

    /* Count frames the harness refused to address. */
    void note_refused(std::uint64_t count) { report_.refused += count; }

    std::size_t pending() const noexcept { return queue_.size(); }
    const ScheduleReportV1& report() const noexcept { return report_; }
    std::uint64_t frame_sequence() const noexcept { return next_sequence_; }

private:
    bool deliver(TransportFrameV1& frame);
    EngineFixture* endpoint(std::uint64_t ordinal) const noexcept;

    EngineFixture* left_ = nullptr;
    EngineFixture* right_ = nullptr;
    std::vector<TransportFrameV1> queue_;
    bool paused_left_ = false;
    bool paused_right_ = false;
    std::uint64_t next_sequence_ = 1;
    ScheduleReportV1 report_{};
};

/* ------------------------------------------------------- the pair */

class TwoEngineFixtureV1 final
{
public:
    TwoEngineFixtureV1();
    ~TwoEngineFixtureV1();

    TwoEngineFixtureV1(const TwoEngineFixtureV1&) = delete;
    TwoEngineFixtureV1& operator=(const TwoEngineFixtureV1&) = delete;

    EngineFixture& left() noexcept { return *left_; }
    EngineFixture& right() noexcept { return *right_; }

    DeterministicSchedulerV1& scheduler() noexcept { return *scheduler_; }
    ResourceLedgerV1& ledger() noexcept { return ledger_; }
    WireCountersV1& wire() noexcept { return wire_; }

    /*
     * Prove the two endpoints really are separate instances: disjoint provider
     * contexts, disjoint durable stores and disjoint crypto evidence. Returns
     * false if anything is shared.
     */
    bool endpoints_are_isolated() const;

    /* Both engines reach READY and expose link actions. */
    void bring_up();

    /*
     * Relay every frame the providers produced since the last call into the
     * scheduler queue, copying the exact bytes the provider emitted. This is the
     * only path by which one engine ever observes the other.
     */
    std::size_t capture_transport();

    /* capture_transport() then deliver everything. */
    std::size_t pump();

    /*
     * Run the pair until no new transport appears, `rounds` times. Returns the
     * number of delivered frames.
     */
    std::size_t pump_until_quiet(std::size_t rounds);

    /* Durable store snapshots, used to simulate a process restart. */
    std::vector<std::uint8_t> object_store_snapshot(std::uint64_t ordinal) const;
    std::vector<std::uint8_t> secure_store_snapshot(std::uint64_t ordinal) const;

    /* Resource oracle. Fails the current test when anything is unbalanced. */
    void check_resources_balanced(const char* phase);

    /* Shut both engines down through the public ABI. */
    void shutdown_both();

    /* Count frames the harness refused to address after the engines went away. */
    void report_more_refused(std::size_t count);

    /*
     * Shut both engines down AND destroy them (owner-destructor semantics)
     * without destroying the fixture, so a scenario can assert the resource
     * oracle sees a fully balanced ledger. Safe to call more than once.
     */
    void release_engines();

    /*
     * True when the engine handed the provider port a fragment whose encoded
     * tag is the type-16 physical ACK. That is direct evidence that the engine
     * encoded wire bytes and wrote them through the public port.
     */
    bool wire_proof_of_encoded_port_write() const noexcept
    {
        const EngineFixture* engines[2] = {left_.get(), right_.get()};
        for (const EngineFixture* engine : engines)
        {
            if (engine != nullptr && engine->discovery.physical_acks > 0 &&
                engine->discovery.last_encoded_port_bytes >= 2)
                return true;
        }
        return false;
    }

private:
    /* Declared before the engines because each engine borrows both. */
    ResourceLedgerV1 ledger_{};
    WireCountersV1 wire_{};
    std::unique_ptr<EngineFixture> left_;
    std::unique_ptr<EngineFixture> right_;
    std::unique_ptr<DeterministicSchedulerV1> scheduler_;
    /* True once the engines were destroyed; keeps the harness off dangling
     * handles even if a scenario keeps driving the scheduler afterwards. */
    bool released_ = false;
};

/* ------------------------------------------------------- QUIC availability */

/*
 * True when the real Rust/Quinn QUIC provider is linked into this binary. A
 * real implementation validates its own arguments, so a null callback table
 * makes create() return null; a missing symbol or a hand-written stub cannot do
 * that. This is the transport loopback_quic_fixture builds on.
 */
bool real_quic_provider_is_linked();

} // namespace flynes::tests::nearby

#endif