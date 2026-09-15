#include <flynes/flynes_session.h>

#include "../harness/deterministic_executor.hpp"
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
#include "link/link_control_contract.hpp"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;
void check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

void retain_noop(void*) {}
void release_noop(void*) {}
fly_session_result_v2 read_clock(void*, fly_session_clock_sample_v2* out)
{
    if (!out) return FLY_SESSION_V2_INVALID_ARGUMENT;
    out->continuous_ns = 1;
    out->suspend_inclusive = 1;
    out->boot_generation[0] = 1;
    return FLY_SESSION_V2_OK;
}

[[maybe_unused]] fly_session_result_v2 unavailable_cancel(
    void*, const fly_session_op_token_v2*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_key_generate(
    void*, const fly_session_op_token_v2*, std::uint32_t,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_key_open(
    void*, const fly_session_op_token_v2*, std::uint32_t,
    fly_session_bytes_v2, fly_session_bytes_v2, const std::uint8_t[32],
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_key_public(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_key_sign(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, fly_session_bytes_v2, const std::uint8_t[32],
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_key_agree(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_key_release(
    void*, fly_session_resource_handle_v2)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_key_destroy(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2,
    std::uint64_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_crypto_random(
    void*, const fly_session_op_token_v2*, std::uint32_t,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_crypto_hkdf(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, std::uint32_t,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_crypto_aead(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, fly_session_bytes_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_crypto_verify(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2,
    fly_session_bytes_v2, const std::uint8_t[32], fly_session_bytes_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_crypto_hmac(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_secret_release(
    void*, fly_session_resource_handle_v2)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_tls_create(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_tls_restore(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2,
    const std::uint8_t[32], fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_tls_release(
    void*, fly_session_resource_handle_v2)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_bearer_start(
    void*, const fly_session_op_token_v2*, const std::uint8_t[32],
    fly_session_resource_handle_v2, std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_bearer_resolve(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_bearer_release(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_prepare_credential(
    void*, const fly_session_op_token_v2*, const std::uint8_t[32],
    fly_session_bytes_v2, fly_session_resource_handle_v2, std::uint32_t,
    fly_session_bytes_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_quic_start(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_resource_handle_v2,
    const fly_session_quic_connect_policy_v2*, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_quic_inspect(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_quic_exporter(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, std::uint32_t,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_quic_stream(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
/* Kept for symmetry with the rest of the unavailable_quic_* stub family; the
 * port table currently wires writes through unavailable_quic_stream, so GCC
 * flags this one as unused while MSVC does not. */
[[maybe_unused]] fly_session_result_v2 unavailable_quic_write(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_buffer_v2_t*, std::uint32_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_quic_control(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint64_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_quic_datagram(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_resource_handle_v2, std::uint64_t, fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_quic_query(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_inbox_v2_t*)
{ return FLY_SESSION_V2_UNAVAILABLE; }
[[maybe_unused]] fly_session_result_v2 unavailable_quic_close(
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
    {
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

const fly_session_action_descriptor_v2* find_action(
    const std::vector<fly_session_action_descriptor_v2>& actions,
    std::uint32_t kind)
{
    for (const auto& action : actions)
        if (action.action_kind == kind) return &action;
    return nullptr;
}

std::vector<std::uint8_t> logical_v1(
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

std::vector<std::uint8_t> pair_context_body()
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

std::array<std::uint8_t, 512> capability_summary()
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

void deliver_capability(EngineFixture& fixture)
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

std::array<std::uint8_t, 65> p256_generator()
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

std::array<std::uint8_t, 65> p256_double_generator()
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

void deliver_provider_buffer(
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

void deliver_provider_end(
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
fly_session_result_v2 deliver_provider_hash_raw(
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

void deliver_provider_hash(
    fly_session_inbox_v2_t* inbox, const fly_session_op_token_v2& token,
    std::uint32_t kind, fly_session_resource_handle_v2 resource,
    const std::array<std::uint8_t, 32>& hash)
{
    check(deliver_provider_hash_raw(inbox, token, kind, resource, hash) ==
              FLY_SESSION_V2_ACCEPTED,
          "typed provider hash completion enters public engine");
}

void deliver_provider_hash_buffer(
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

void deliver_provider_resource(
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

void deliver_provider_resource_pair(
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

void deliver_provider_stream_data(
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

void deliver_discovery_end(EngineFixture& fixture);

void consume_physical_acks(EngineFixture& fixture, std::size_t)
{
    fixture.executor.run_all();
}

void deliver_logical(EngineFixture& fixture,
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

void deliver_discovery_end(EngineFixture& fixture)
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

void submit(EngineFixture& fixture,
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

void submit_reference(EngineFixture& fixture,
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

void start_responder_probe(EngineFixture& fixture, std::uint64_t request_id)
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

void deliver_cancelled_provider_terminal(
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

void test_public_ready_and_invitation_projection()
{
    EngineFixture inviter;
    EngineFixture joiner;
    inviter.platform.ready();
    joiner.platform.ready();
    inviter.executor.run_all();
    joiner.executor.run_all();

    std::vector<fly_session_action_descriptor_v2> inviter_actions;
    std::vector<fly_session_action_descriptor_v2> joiner_actions;
    const auto inviter_ready = inviter.snapshot(&inviter_actions);
    const auto joiner_ready = joiner.snapshot(&joiner_actions);
    check(inviter_ready.engine_state == FLY_SESSION_ENGINE_READY_V2 &&
              inviter_ready.link_state == FLY_SESSION_LINK_IDLE_V2 &&
              joiner_ready.engine_state == FLY_SESSION_ENGINE_READY_V2 &&
              joiner_ready.link_state == FLY_SESSION_LINK_IDLE_V2,
          "both public engines project ready idle link facts");
    const auto* create = find_action(inviter_actions,
                                     FLY_SESSION_ACTION_CREATE_INVITE_V2);
    const auto* join = find_action(joiner_actions, FLY_SESSION_ACTION_JOIN_CODE_V2);
    check(create && create->enabled && create->approval_token,
          "ready view publishes create-invite approval token");
    check(join && join->enabled && join->approval_token,
          "ready view publishes join-code approval token");
    if (!create || !join) return;

    submit(inviter, *create, 101, false);
    submit(joiner, *join, 201, true);
    check(inviter.discovery.advertisements == 1 &&
              inviter.discovery.scans == 0 &&
              inviter.discovery.last_token.scope.kind ==
                  FLY_SESSION_SCOPE_LINK_V2 &&
              inviter.discovery.last_token.connection_generation != 0 &&
              inviter.discovery.last_token.operation_id > 1,
          "create dispatches one fenced advertisement operation");
    check(joiner.discovery.scans == 1 &&
              joiner.discovery.advertisements == 0 &&
              joiner.discovery.last_token.scope.kind ==
                  FLY_SESSION_SCOPE_LINK_V2 &&
              joiner.discovery.last_token.connection_generation != 0 &&
              joiner.discovery.last_token.operation_id > 1,
          "join dispatches one fenced scan operation");
    check(inviter.snapshot().link_state == FLY_SESSION_LINK_INVITING_V2,
          "create action moves only inviter to inviting");
    check(joiner.snapshot().link_state == FLY_SESSION_LINK_JOINING_V2,
          "join action moves only joiner to joining");

    fly_session_port_event_v2 connection{};
    connection.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    connection.abi_version = FLY_SESSION_ABI_VERSION_2;
    connection.token = joiner.discovery.last_token;
    connection.event_sequence = 1;
    connection.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    connection.terminal = 1;
    connection.result = FLY_SESSION_V2_OK;
    connection.payload_kind = FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2;
    fly_session_provider_resource_event_v2 connection_payload{};
    connection_payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    connection_payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    connection_payload.resource = 41;
    connection_payload.generation = connection.token.connection_generation;
    connection_payload.value0 = FLY_SESSION_DISCOVERY_PHYSICAL_CENTRAL_V2;
    connection_payload.value1 = 23;
    connection.payload_size = sizeof(connection_payload) - 1;
    std::memcpy(connection.payload, &connection_payload,
                connection.payload_size);
    check(fly_session_deliver_v2(joiner.discovery.inbox, &connection) ==
              FLY_SESSION_V2_ABI_MISMATCH &&
              joiner.snapshot().link_state == FLY_SESSION_LINK_JOINING_V2,
          "malformed discovery connection is rejected before reducer effects");
    connection.payload_size = sizeof(connection_payload);
    std::memcpy(connection.payload, &connection_payload,
                sizeof(connection_payload));
    check(fly_session_deliver_v2(joiner.discovery.inbox, &connection) ==
              FLY_SESSION_V2_ACCEPTED,
          "strict typed discovery connection queues");
    joiner.executor.run_all();
    check(joiner.snapshot().link_state == FLY_SESSION_LINK_AUTHENTICATING_V2 &&
              joiner.discovery.subscriptions == 1,
          "validated discovery connection starts one fenced raw GATT subscription");

    const std::array<std::uint8_t, 6> lookup_code{{'0', '1', '2', '3', '4', '5'}};
    std::array<std::uint8_t, 16> lookup_nonce{};
    lookup_nonce[0] = 1;
    std::array<std::uint8_t, 72> logical{};
    std::size_t logical_written = 0;
    check(flynes::session::wire::encode_gatt_lookup_request_v2(
              lookup_code, lookup_nonce, logical.data(), logical.size(),
              &logical_written) == flynes::session::wire::Status::Ok,
          "public-engine GATT fixture encodes one exact logical message");
    std::vector<std::vector<std::uint8_t>> fragments;
    check(flynes::session::wire::fragment_gatt_logical(
              logical.data(), logical_written, 27, 1, 20, &fragments) ==
              flynes::session::wire::GattFragmentResult::Accepted,
          "public-engine GATT fixture uses the 20-byte fallback");
    const std::size_t lookup_ack_begin =
        joiner.discovery.written_fragments.size();
    std::uint64_t event_sequence = 1;
    for (const auto& fragment : fragments)
    {
        fly_session_buffer_v2_t* buffer = nullptr;
        const fly_session_bytes_v2 source{
            fragment.data(), static_cast<std::uint32_t>(fragment.size()), 0};
        check(fly_session_buffer_create_copy_v2(source, &buffer) ==
                  FLY_SESSION_V2_OK,
              "raw GATT fragment copied into an immutable buffer");
        fly_session_port_event_v2 bytes_event{};
        bytes_event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
        bytes_event.abi_version = FLY_SESSION_ABI_VERSION_2;
        bytes_event.token = joiner.discovery.last_token;
        bytes_event.event_sequence = event_sequence++;
        bytes_event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
        bytes_event.result = FLY_SESSION_V2_OK;
        bytes_event.payload_kind = FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2;
        fly_session_provider_buffer_event_v2 bytes_payload{};
        bytes_payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
        bytes_payload.abi_version = FLY_SESSION_ABI_VERSION_2;
        bytes_payload.buffer = buffer;
        bytes_payload.logical_size = fragment.size();
        bytes_payload.generation =
            bytes_event.token.connection_generation;
        bytes_event.payload_size = sizeof(bytes_payload);
        std::memcpy(bytes_event.payload, &bytes_payload, sizeof(bytes_payload));
        auto deliver_result =
            fly_session_deliver_v2(joiner.discovery.inbox, &bytes_event);
        if (deliver_result == FLY_SESSION_V2_BACKPRESSURE)
        {
            joiner.executor.run_all();
            deliver_result =
                fly_session_deliver_v2(joiner.discovery.inbox, &bytes_event);
        }
        check(deliver_result == FLY_SESSION_V2_ACCEPTED,
              "fenced raw GATT fragment enters the public inbox");
        fly_session_buffer_release_v2(buffer);
    }
    consume_physical_acks(joiner, lookup_ack_begin);
    check(joiner.snapshot().link_state == FLY_SESSION_LINK_AUTHENTICATING_V2,
          "a valid lookup frame is reassembled but cannot bypass authentication");

    const auto pair_context = logical_v1(
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::PairContext),
        pair_context_body());
    deliver_logical(
        joiner, pair_context,
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::PairContext),
        2, 100);
    joiner.executor.run_all();
    check(joiner.snapshot().link_state == FLY_SESSION_LINK_AUTHENTICATING_V2 &&
              joiner.bearer.probes == 1 && joiner.key.generates == 0,
          "exact PairContext probes bearer capability before creating keys");
    check(joiner.discovery.physical_acks > 0,
          "accepted logical messages emit ordered type-16 physical ACK fragments");
    deliver_capability(joiner);
    joiner.executor.run_all();
    check(joiner.key.generates == 1 &&
              joiner.key.last_purpose == FLY_SESSION_KEY_DEVICE_IDENTITY_V2 &&
              joiner.key.last_binding == pair_context_body(),
          "validated capability dispatches the first purpose-bound key operation");

    std::array<std::uint8_t, 32> handle_hash{};
    handle_hash[0] = 1;
    deliver_provider_hash(
        joiner.key.inbox, joiner.key.last_token,
        FLY_SESSION_PROVIDER_KEY_HANDLE_V2, 51, handle_hash);
    joiner.executor.run_all();
    check(joiner.key.generates == 2 &&
              joiner.key.last_purpose == FLY_SESSION_KEY_PAIR_ECDH_V2,
          "identity terminal advances only to PAIR_ECDH generation");
    deliver_provider_hash(
        joiner.key.inbox, joiner.key.last_token,
        FLY_SESSION_PROVIDER_KEY_HANDLE_V2, 52, handle_hash);
    joiner.executor.run_all();
    check(joiner.key.generates == 3 &&
              joiner.key.last_purpose == FLY_SESSION_KEY_TLS_V2,
          "ECDH terminal advances only to TLS generation");
    deliver_provider_hash(
        joiner.key.inbox, joiner.key.last_token,
        FLY_SESSION_PROVIDER_KEY_HANDLE_V2, 53, handle_hash);
    joiner.executor.run_all();
    check(joiner.key.public_reads == 1 && joiner.key.last_resource == 51 &&
              joiner.key.last_encoding ==
                  FLY_SESSION_PUBLIC_KEY_X963_UNCOMPRESSED_V2,
          "identity public point is read through the exact provider handle");

    const auto point = p256_generator();
    deliver_provider_buffer(
        joiner.key.inbox, joiner.key.last_token,
        FLY_SESSION_PROVIDER_KEY_PUBLIC_V2, point.data(), point.size());
    joiner.executor.run_all();
    check(joiner.key.public_reads == 2 && joiner.key.last_resource == 52 &&
              joiner.key.last_encoding ==
                  FLY_SESSION_PUBLIC_KEY_X963_UNCOMPRESSED_V2,
          "ECDH public point follows identity public point");
    deliver_provider_buffer(
        joiner.key.inbox, joiner.key.last_token,
        FLY_SESSION_PROVIDER_KEY_PUBLIC_V2, point.data(), point.size());
    joiner.executor.run_all();
    check(joiner.key.public_reads == 3 && joiner.key.last_resource == 53 &&
              joiner.key.last_encoding == FLY_SESSION_PUBLIC_KEY_DER_SPKI_V2,
          "TLS provider is read as exact DER-SPKI");

    const std::array<std::uint8_t, 4> tls_spki{{0x30, 0x02, 0x01, 0x01}};
    deliver_provider_buffer(
        joiner.key.inbox, joiner.key.last_token,
        FLY_SESSION_PROVIDER_KEY_PUBLIC_V2,
        tls_spki.data(), tls_spki.size());
    joiner.executor.run_all();
    check(joiner.tls.creates == 1 && joiner.tls.last_key == 53,
          "TLS material is created from the same pinned TLS key");
    const auto tls_hash = flynes::session::wire::sha256(
        tls_spki.data(), tls_spki.size());
    deliver_provider_hash(
        joiner.tls.inbox, joiner.tls.last_token,
        FLY_SESSION_PROVIDER_TLS_MATERIAL_V2, 54, tls_hash);
    joiner.executor.run_all();
    check(joiner.crypto.randoms == 1 && joiner.crypto.last_size == 32,
          "TLS pin match advances to one 32-byte contribution nonce");
    std::array<std::uint8_t, 32> nonce{};
    std::fill(nonce.begin(), nonce.end(), std::uint8_t{0x55});
    deliver_provider_buffer(
        joiner.crypto.inbox, joiner.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2, nonce.data(), nonce.size());
    joiner.executor.run_all();
    check(joiner.snapshot().link_state == FLY_SESSION_LINK_AUTHENTICATING_V2,
          "complete local material still cannot claim authenticated lobby");

    flynes::session::wire::PairContextV1 decoded_context{};
    const auto context_bytes = pair_context_body();
    check(flynes::session::wire::decode_pair_context_v1(
              context_bytes.data(), context_bytes.size(), &decoded_context) ==
              flynes::session::wire::Status::Ok,
          "test reconstructs the canonical pair context");
    const auto capability = capability_summary();
    const auto capability_hash = flynes::session::wire::domain_hash(
        "flynes-pair-capability-summary-v1", capability.data(),
        capability.size());
    std::array<std::uint8_t, 32> initiator_nonce{};
    std::fill(initiator_nonce.begin(), initiator_nonce.end(),
              std::uint8_t{0x44});
    std::array<std::uint8_t, 320> initiator_contribution_bytes{};
    check(flynes::session::wire::encode_pair_contribution_v1(
              decoded_context,
              flynes::session::wire::PairRoleV1::Initiator, point, point,
              tls_hash, initiator_nonce, capability_hash,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &initiator_contribution_bytes) ==
              flynes::session::wire::Status::Ok,
          "test encodes the initiator contribution canonically");
    flynes::session::wire::PairContributionV1 initiator_contribution{};
    check(flynes::session::wire::decode_pair_contribution_v1(
              initiator_contribution_bytes.data(),
              initiator_contribution_bytes.size(), decoded_context,
              flynes::session::wire::PairRoleV1::Initiator,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &initiator_contribution) ==
              flynes::session::wire::Status::Ok,
          "test decodes the exact initiator contribution");
    std::array<std::uint8_t, 152> initiator_commit_bytes{};
    check(flynes::session::wire::encode_pair_commit_v1(
              decoded_context, initiator_contribution,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &initiator_commit_bytes) ==
              flynes::session::wire::Status::Ok,
          "test encodes the initiator commit canonically");
    const std::vector<std::uint8_t> initiator_commit_body(
        initiator_commit_bytes.begin(), initiator_commit_bytes.end());
    deliver_logical(
        joiner,
        logical_v1(static_cast<std::uint8_t>(
                       flynes::session::wire::GattLogicalType::PairCommit),
                   initiator_commit_body),
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::PairCommit),
        3, 200);
    joiner.executor.run_all();
    check(joiner.discovery.writes == 1 &&
              joiner.discovery.written_fragments.size() == 1,
          "initiator commit starts exactly one responder commit fragment");

    std::size_t completed_writes = 0;
    for (std::size_t guard = 0;
         completed_writes < joiner.discovery.written_fragments.size() &&
         guard < 128;
         ++guard)
    {
        deliver_discovery_end(joiner);
        ++completed_writes;
        joiner.executor.run_all();
    }
    check(completed_writes == joiner.discovery.written_fragments.size() &&
              completed_writes > 1 && completed_writes < 128,
          "each responder commit fragment requires its own typed terminal");

    flynes::session::wire::GattReassembler outgoing_reassembler(
        joiner.discovery.write_token.connection_generation,
        flynes::session::wire::GattPhysicalDirection::CentralToPeripheral);
    std::vector<std::uint8_t> responder_commit_logical;
    auto outgoing_result =
        flynes::session::wire::GattFragmentResult::InvalidArgument;
    std::uint64_t outgoing_now = 1;
    for (const auto& fragment : joiner.discovery.written_fragments)
    {
        outgoing_result = outgoing_reassembler.accept(
            joiner.discovery.write_token.connection_generation,
            outgoing_now++, fragment.data(), fragment.size(),
            &responder_commit_logical);
    }
    check(outgoing_result ==
              flynes::session::wire::GattFragmentResult::Complete,
          "captured responder fragments reassemble to one logical message");
    flynes::session::wire::GattLogicalMessageView responder_message{};
    check(flynes::session::wire::decode_gatt_logical_message(
              responder_commit_logical.data(), responder_commit_logical.size(),
              static_cast<std::uint8_t>(
                  flynes::session::wire::GattLogicalType::PairCommit),
              &responder_message) ==
              flynes::session::wire::GattFragmentResult::Complete,
          "outgoing responder commit has the exact logical envelope");
    std::array<std::uint8_t, 320> responder_contribution_bytes{};
    check(flynes::session::wire::encode_pair_contribution_v1(
              decoded_context,
              flynes::session::wire::PairRoleV1::Responder, point, point,
              tls_hash, nonce, capability_hash,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &responder_contribution_bytes) ==
              flynes::session::wire::Status::Ok,
          "test reconstructs the provider-backed responder contribution");
    flynes::session::wire::PairContributionV1 responder_contribution{};
    check(flynes::session::wire::decode_pair_contribution_v1(
              responder_contribution_bytes.data(),
              responder_contribution_bytes.size(), decoded_context,
              flynes::session::wire::PairRoleV1::Responder,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &responder_contribution) ==
              flynes::session::wire::Status::Ok,
          "test decodes the provider-backed responder contribution");
    std::array<std::uint8_t, 152> expected_responder_commit{};
    check(flynes::session::wire::encode_pair_commit_v1(
              decoded_context, responder_contribution,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &expected_responder_commit) ==
              flynes::session::wire::Status::Ok &&
              responder_message.body_size == expected_responder_commit.size() &&
              std::equal(expected_responder_commit.begin(),
                         expected_responder_commit.end(),
                         responder_message.body),
          "public engine sends the exact responder commit from provider material");

    const auto commit_fragment_count = completed_writes;
    check(joiner.key.agreements == 1 && joiner.key.last_resource == 52,
          "both commits dispatch ECDH through the PAIR_ECDH handle");
    deliver_provider_resource(
        joiner.key.inbox, joiner.key.last_token,
        FLY_SESSION_PROVIDER_KEY_AGREEMENT_V2, 61);
    joiner.executor.run_all();
    check(joiner.crypto.hkdfs == 1 && joiner.crypto.last_resource == 61 &&
              std::string(joiner.crypto.last_info.begin(),
                          joiner.crypto.last_info.end()) ==
                  "flynes-pair-reveal-i2r-v1",
          "public engine derives the exact i2r preidentity key");
    deliver_provider_resource(
        joiner.crypto.inbox, joiner.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 62);
    joiner.executor.run_all();
    check(joiner.crypto.hkdfs == 2 && joiner.crypto.last_resource == 61 &&
              std::string(joiner.crypto.last_info.begin(),
                          joiner.crypto.last_info.end()) ==
                  "flynes-pair-reveal-r2i-v1",
          "public engine derives the separate r2i preidentity key");
    deliver_provider_resource(
        joiner.crypto.inbox, joiner.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 63);
    joiner.executor.run_all();

    std::array<std::uint8_t, 12> initiator_reveal_nonce{};
    initiator_reveal_nonce[0] = 0x71;
    std::array<std::uint8_t,
               flynes::session::wire::kPairRevealCiphertextAndTagSizeV1>
        initiator_ciphertext{};
    initiator_ciphertext[0] = 0x72;
    std::array<std::uint8_t,
               flynes::session::wire::kPairRevealBodySizeV1>
        initiator_reveal_body{};
    check(flynes::session::wire::encode_pair_reveal_envelope_v1(
              initiator_reveal_nonce, initiator_ciphertext,
              &initiator_reveal_body) == flynes::session::wire::Status::Ok,
          "initiator reveal fixture encodes the exact envelope");
    deliver_logical(
        joiner,
        logical_v1(static_cast<std::uint8_t>(
                       flynes::session::wire::GattLogicalType::PairReveal),
                   std::vector<std::uint8_t>(initiator_reveal_body.begin(),
                                             initiator_reveal_body.end())),
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::PairReveal),
        4, 400);
    joiner.executor.run_all();
    check(joiner.crypto.aead_opens == 1 &&
              joiner.crypto.last_resource == 62 &&
              joiner.crypto.last_nonce == std::vector<std::uint8_t>(
                  initiator_reveal_nonce.begin(), initiator_reveal_nonce.end()) &&
              joiner.crypto.last_aad.size() == 124 &&
              joiner.crypto.last_input == std::vector<std::uint8_t>(
                  initiator_ciphertext.begin(), initiator_ciphertext.end()),
          "public engine opens initiator reveal with exact i2r AEAD inputs");
    deliver_provider_buffer(
        joiner.crypto.inbox, joiner.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        initiator_contribution_bytes.data(), initiator_contribution_bytes.size());
    joiner.executor.run_all();
    check(joiner.crypto.randoms == 2 && joiner.crypto.last_size == 12,
          "verified initiator contribution unlocks one reveal nonce");
    std::array<std::uint8_t, 12> responder_reveal_nonce{};
    responder_reveal_nonce[0] = 0x73;
    deliver_provider_buffer(
        joiner.crypto.inbox, joiner.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2,
        responder_reveal_nonce.data(), responder_reveal_nonce.size());
    joiner.executor.run_all();
    check(joiner.crypto.aead_seals == 1 &&
              joiner.crypto.last_resource == 63 &&
              joiner.crypto.last_input == std::vector<std::uint8_t>(
                  responder_contribution_bytes.begin(),
                  responder_contribution_bytes.end()),
          "public engine seals responder contribution with the r2i key");
    std::array<std::uint8_t,
               flynes::session::wire::kPairRevealCiphertextAndTagSizeV1>
        responder_ciphertext{};
    responder_ciphertext[0] = 0x74;
    deliver_provider_buffer(
        joiner.crypto.inbox, joiner.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        responder_ciphertext.data(), responder_ciphertext.size());
    joiner.executor.run_all();
    check(joiner.discovery.written_fragments.size() ==
              commit_fragment_count + 1,
          "sealed responder reveal starts one ordered GATT fragment");
    std::size_t reveal_completed = commit_fragment_count;
    for (std::size_t guard = 0;
         reveal_completed < joiner.discovery.written_fragments.size() &&
         guard < 128;
         ++guard)
    {
        deliver_discovery_end(joiner);
        ++reveal_completed;
        joiner.executor.run_all();
    }
    check(reveal_completed == joiner.discovery.written_fragments.size() &&
              reveal_completed > commit_fragment_count + 1,
          "responder reveal waits for every physical write terminal");
    flynes::session::wire::GattReassembler reveal_reassembler(
        joiner.discovery.write_token.connection_generation,
        flynes::session::wire::GattPhysicalDirection::CentralToPeripheral);
    std::vector<std::uint8_t> responder_reveal_logical;
    auto reveal_result = flynes::session::wire::GattFragmentResult::InvalidArgument;
    for (std::size_t index = commit_fragment_count;
         index < joiner.discovery.written_fragments.size(); ++index)
    {
        const auto& fragment = joiner.discovery.written_fragments[index];
        reveal_result = reveal_reassembler.accept(
            joiner.discovery.write_token.connection_generation,
            index + 1, fragment.data(), fragment.size(),
            &responder_reveal_logical);
    }
    flynes::session::wire::GattLogicalMessageView reveal_message{};
    check(reveal_result == flynes::session::wire::GattFragmentResult::Complete &&
              flynes::session::wire::decode_gatt_logical_message(
                  responder_reveal_logical.data(),
                  responder_reveal_logical.size(),
                  static_cast<std::uint8_t>(
                      flynes::session::wire::GattLogicalType::PairReveal),
                  &reveal_message) ==
                  flynes::session::wire::GattFragmentResult::Complete,
          "outgoing responder reveal reassembles as exact type 5");
    std::array<std::uint8_t,
               flynes::session::wire::kPairRevealBodySizeV1>
        expected_reveal_body{};
    check(flynes::session::wire::encode_pair_reveal_envelope_v1(
              responder_reveal_nonce, responder_ciphertext,
              &expected_reveal_body) == flynes::session::wire::Status::Ok &&
              reveal_message.body_size == expected_reveal_body.size() &&
              std::equal(expected_reveal_body.begin(),
                         expected_reveal_body.end(), reveal_message.body) &&
              joiner.snapshot().link_state == FLY_SESSION_LINK_AUTHENTICATING_V2,
          "public engine sends provider ciphertext but does not overclaim authentication");

    const auto premature_key_confirm = logical_v1(
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::KeyConfirm), {});
    deliver_logical(
        joiner, premature_key_confirm,
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::KeyConfirm),
        5, 600);
    joiner.executor.run_all();
    check(joiner.snapshot().link_state == FLY_SESSION_LINK_FAILED_V2 &&
              joiner.discovery.disconnects == 1 &&
              joiner.key.releases == 3 && joiner.tls.releases == 1 &&
              joiner.crypto.releases == 3,
          "premature authentication phase releases all local material and GATT");

    std::vector<fly_session_action_descriptor_v2> inviting_actions;
    const auto inviting = inviter.snapshot(&inviting_actions);
    const auto* cancel = find_action(inviting_actions,
                                     FLY_SESSION_ACTION_CANCEL_INVITE_V2);
    check(cancel && inviting.link_state == FLY_SESSION_LINK_INVITING_V2,
          "inviting view publishes exact cancel action");
    if (cancel)
    {
        submit(inviter, *cancel, 102, false);
        check(inviter.discovery.stops == 1 &&
                  inviter.snapshot().link_state == FLY_SESSION_LINK_IDLE_V2,
              "cancel stops exact discovery operation and returns idle");
        fly_session_port_event_v2 stale{};
        stale.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
        stale.abi_version = FLY_SESSION_ABI_VERSION_2;
        stale.token = inviter.discovery.last_token;
        stale.event_sequence = 99;
        stale.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
        stale.terminal = 1;
        stale.result = FLY_SESSION_V2_OK;
        stale.payload_kind = FLY_SESSION_PROVIDER_DISCOVERY_END_V2;
        fly_session_provider_end_event_v2 payload{};
        payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
        payload.abi_version = FLY_SESSION_ABI_VERSION_2;
        stale.payload_size = sizeof(payload);
        std::memcpy(stale.payload, &payload, sizeof(payload));
        check(fly_session_deliver_v2(inviter.platform.inbox, &stale) ==
                  FLY_SESSION_V2_STALE,
              "late discovery terminal after synchronous cancel is stale");
    }
    for (auto& action : inviter_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : joiner_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : inviting_actions)
        fly_session_approval_token_release_v2(action.approval_token);
}

void test_connection_without_secure_pairing_ports_fails_closed()
{
    EngineFixture fixture(false);
    fixture.platform.ready();
    fixture.executor.run_all();
    std::vector<fly_session_action_descriptor_v2> actions;
    fixture.snapshot(&actions);
    const auto* join = find_action(actions, FLY_SESSION_ACTION_JOIN_CODE_V2);
    check(join != nullptr, "discovery-only fixture can begin anonymous routing");
    if (!join) return;
    submit(fixture, *join, 250, true);

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
    payload.resource = 42;
    payload.generation = event.token.connection_generation;
    payload.value0 = FLY_SESSION_DISCOVERY_PHYSICAL_CENTRAL_V2;
    payload.value1 = 23;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(fixture.discovery.inbox, &event) ==
              FLY_SESSION_V2_ACCEPTED,
          "discovery completion is admitted before capability reduction");
    fixture.executor.run_all();
    check(fixture.snapshot().link_state == FLY_SESSION_LINK_FAILED_V2 &&
              fixture.discovery.subscriptions == 0 &&
              fixture.discovery.disconnects == 1,
          "missing Key/Crypto/TLS providers fail closed and release GATT");
    for (auto& action : actions)
        fly_session_approval_token_release_v2(action.approval_token);
}

void test_shutdown_waits_for_async_discovery_terminal()
{
    EngineFixture fixture;
    fixture.discovery.asynchronous_stop = true;
    fixture.platform.ready();
    fixture.executor.run_all();

    std::vector<fly_session_action_descriptor_v2> actions;
    fixture.snapshot(&actions);
    const auto* create = find_action(actions, FLY_SESSION_ACTION_CREATE_INVITE_V2);
    check(create != nullptr, "shutdown fixture exposes create invite");
    if (!create) return;
    submit(fixture, *create, 301, false);
    check(fly_session_begin_shutdown_v2(fixture.engine, 302) ==
              FLY_SESSION_V2_ACCEPTED,
          "shutdown with active discovery is accepted");
    check(fixture.discovery.stops == 1,
          "shutdown requests the exact active discovery stop");
    check(fly_session_destroy_v2(fixture.engine) == FLY_SESSION_V2_BUSY,
          "destroy stays busy while discovery stop is asynchronous");

    fly_session_port_event_v2 terminal{};
    terminal.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    terminal.abi_version = FLY_SESSION_ABI_VERSION_2;
    terminal.token = fixture.discovery.last_token;
    terminal.event_sequence = 2;
    terminal.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    terminal.terminal = 1;
    terminal.result = FLY_SESSION_V2_CANCELLED;
    terminal.payload_kind = FLY_SESSION_PROVIDER_DISCOVERY_END_V2;
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    terminal.payload_size = sizeof(payload);
    std::memcpy(terminal.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(fixture.discovery.inbox, &terminal) ==
              FLY_SESSION_V2_ACCEPTED,
          "exact asynchronous discovery terminal is accepted");
    fixture.executor.run_all();
    check(fly_session_destroy_v2(fixture.engine) == FLY_SESSION_V2_OK,
          "destroy succeeds only after discovery terminal is consumed");
    fixture.engine = nullptr;
    for (auto& action : actions)
        fly_session_approval_token_release_v2(action.approval_token);
}

void test_shutdown_disconnects_late_discovery_connection()
{
    EngineFixture fixture;
    fixture.discovery.asynchronous_stop = true;
    fixture.platform.ready();
    fixture.executor.run_all();

    std::vector<fly_session_action_descriptor_v2> actions;
    fixture.snapshot(&actions);
    const auto* create = find_action(actions, FLY_SESSION_ACTION_CREATE_INVITE_V2);
    check(create != nullptr, "late-connection fixture exposes create invite");
    if (!create) return;
    submit(fixture, *create, 311, false);
    const auto discovery_token = fixture.discovery.last_token;
    check(fly_session_begin_shutdown_v2(fixture.engine, 312) ==
              FLY_SESSION_V2_ACCEPTED,
          "shutdown accepts an asynchronously stopping discovery operation");

    fly_session_port_event_v2 connection{};
    connection.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    connection.abi_version = FLY_SESSION_ABI_VERSION_2;
    connection.token = discovery_token;
    connection.event_sequence = 2;
    connection.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    connection.terminal = 1;
    connection.result = FLY_SESSION_V2_OK;
    connection.payload_kind = FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2;
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = 91;
    payload.generation = discovery_token.connection_generation;
    payload.value0 = FLY_SESSION_DISCOVERY_PHYSICAL_PERIPHERAL_V2;
    payload.value1 = 23;
    connection.payload_size = sizeof(payload);
    std::memcpy(connection.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(fixture.discovery.inbox, &connection) ==
              FLY_SESSION_V2_ACCEPTED,
          "a valid connection may win the race with discovery cancellation");
    fixture.executor.run_all();
    check(fixture.discovery.subscriptions == 0,
          "shutdown never subscribes a late discovery connection");
    check(fixture.discovery.disconnects == 1,
          "shutdown closes the late provider-owned connection exactly once");
    check(fly_session_destroy_v2(fixture.engine) == FLY_SESSION_V2_OK,
          "destroy succeeds after the late connection is disconnected");
    fixture.engine = nullptr;
    for (auto& action : actions)
        fly_session_approval_token_release_v2(action.approval_token);
}

void test_shutdown_waits_for_active_bearer_probe()
{
    EngineFixture fixture;
    start_responder_probe(fixture, 321);
    check(fixture.bearer.probes == 1,
          "shutdown fixture reaches one accepted bearer probe");

    check(fly_session_begin_shutdown_v2(fixture.engine, 322) ==
              FLY_SESSION_V2_ACCEPTED && fixture.bearer.cancels == 1,
          "shutdown cancels the exact active bearer operation");
    const auto early_destroy = fly_session_destroy_v2(fixture.engine);
    check(early_destroy == FLY_SESSION_V2_BUSY,
          "destroy waits for the bearer cancellation terminal");
    if (early_destroy == FLY_SESSION_V2_OK)
    {
        fixture.engine = nullptr;
        return;
    }

    deliver_cancelled_provider_terminal(
        fixture.bearer.inbox, fixture.bearer.last_token,
        FLY_SESSION_PROVIDER_BEARER_CAPABILITIES_V2);
    fixture.executor.run_all();
    check(fly_session_destroy_v2(fixture.engine) == FLY_SESSION_V2_OK,
          "destroy succeeds after the bearer terminal is consumed");
    fixture.engine = nullptr;
}

void test_shutdown_waits_for_active_pair_material_and_releases_racing_key()
{
    EngineFixture fixture;
    start_responder_probe(fixture, 331);
    deliver_capability(fixture);
    fixture.executor.run_all();
    check(fixture.key.generates == 1,
          "shutdown fixture reaches one accepted identity-key operation");
    fixture.key.cancel_result = FLY_SESSION_V2_ACCEPTED;

    check(fly_session_begin_shutdown_v2(fixture.engine, 332) ==
              FLY_SESSION_V2_ACCEPTED && fixture.key.cancels == 1,
          "shutdown cancels the exact active key operation");
    check(fly_session_destroy_v2(fixture.engine) == FLY_SESSION_V2_BUSY,
          "destroy waits for the key cancellation terminal");

    std::array<std::uint8_t, 32> handle_hash{};
    handle_hash[0] = 1;
    deliver_provider_hash(
        fixture.key.inbox, fixture.key.last_token,
        FLY_SESSION_PROVIDER_KEY_HANDLE_V2, 151, handle_hash);
    fixture.executor.run_all();
    check(fixture.key.releases == 1,
          "a key that wins the cancellation race is released exactly once");
    check(fly_session_destroy_v2(fixture.engine) == FLY_SESSION_V2_OK,
          "destroy succeeds after the racing key terminal is consumed");
    fixture.engine = nullptr;
}

// Selects how the shared initiator flow ends once the public engine has issued
// the 0x0212 immutable-object persistence request.
enum class SessionSigningTail
{
    DurableAcrossBothGates,
    WrongObjectHash,
    ShutdownCancelsThroughObjectStore,
    /* R3 durable re-read negatives: the object is absent, and the object exists
     * but the bytes read back do not match the expected content hash. */
    ReadObjectMissing,
    ReadObjectContentMismatch
};

// The hash domain the wire codec uses for the 0x0212 binding object. Verified
// source of truth: shared/src/session/wire/session_signing_binding.cpp:117 and
// shared/src/session/wire/session_signing_binding.cpp:155, mirrored by the
// 0x0212 codec table row in shared/src/session/wire/session_codec.cpp:397.
constexpr char kSessionSigningBindingHashDomainV1[] =
    "flynes-session-signing-key-binding-hash-v1";

// Drives the tail of the public initiator flow: SecureStore revision, the
// immutable 0x0212 object request it releases, and the object completion.
void drive_session_signing_persistence(
    EngineFixture& fixture, SessionSigningTail tail,
    const std::array<std::uint8_t, 32>& transcript_hash,
    const std::array<std::uint8_t, 16>& session_id,
    const std::array<std::uint8_t, 65>& identity_public_key)
{
    check(fixture.key.generates == 4 &&
              fixture.key.last_purpose == FLY_SESSION_KEY_SESSION_SIGNING_V2 &&
              fixture.key.last_binding.size() == 49,
          "CHANNEL_BOUND starts one independently scoped session signing key");
    const auto session_signing_public = p256_double_generator();
    const auto session_signing_public_hash =
        flynes::session::wire::sha256(
            session_signing_public.data(), session_signing_public.size());
    deliver_provider_hash(
        fixture.key.inbox, fixture.key.last_token,
        FLY_SESSION_PROVIDER_KEY_HANDLE_V2, 318,
        session_signing_public_hash);
    fixture.executor.run_all();
    check(fixture.key.public_reads == 4 &&
              fixture.key.last_resource == 318,
          "public engine reads the generated session signing public key");
    deliver_provider_buffer(
        fixture.key.inbox, fixture.key.last_token,
        FLY_SESSION_PROVIDER_KEY_PUBLIC_V2,
        session_signing_public.data(), session_signing_public.size());
    fixture.executor.run_all();
    check(fixture.key.signs == 2 && fixture.key.last_resource == 171 &&
              fixture.key.last_domain == std::vector<std::uint8_t>(
                  {'f','l','y','n','e','s','-','s','e','s','s','i','o','n','-',
                   's','i','g','n','i','n','g','-','k','e','y','-','b','i','n','d',
                   'i','n','g','-','v','1'}),
          "long-term identity signs the session-key binding once");
    std::array<std::uint8_t, 64> binding_signature{};
    std::fill(binding_signature.begin(), binding_signature.begin() + 32,
              std::uint8_t{0x77});
    std::fill(binding_signature.begin() + 32, binding_signature.end(),
              std::uint8_t{0x44});
    deliver_provider_buffer(
        fixture.key.inbox, fixture.key.last_token,
        FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2,
        binding_signature.data(), binding_signature.size());
    fixture.executor.run_all();
    check(fixture.secure_store.writes == 7 &&
              fixture.secure_store.last_namespace ==
                  std::vector<std::uint8_t>(
                      {'f','l','y','n','e','s','/','s','e','s','s','i','o','n','-',
                       's','i','g','n','i','n','g','/','v','1'}) &&
              fixture.secure_store.last_record_key.size() == 17 &&
              fixture.secure_store.last_expected_revision == 0 &&
              fixture.secure_store.last_value.size() == 349,
          "exact binding and durable KeyRef are persisted before LINK_HELLO");

    // Rebuild the canonical binding and its object hash through the production
    // wire codec so every expected value below is codec-derived, never guessed.
    std::array<std::uint8_t,
               flynes::session::wire::kSessionSigningBindingPretagSizeV1>
        expected_pretag{};
    std::array<std::uint8_t, 32> expected_digest{};
    std::array<std::uint8_t,
               flynes::session::wire::kSessionSigningBindingSizeV1>
        expected_binding{};
    std::array<std::uint8_t, 32> expected_hash{};
    check(flynes::session::wire::build_session_signing_binding_pretag_v1(
              transcript_hash, session_id,
              flynes::session::wire::PairRoleV1::Initiator,
              identity_public_key, session_signing_public,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &expected_pretag, &expected_digest) ==
              flynes::session::wire::Status::Ok &&
              flynes::session::wire::finish_session_signing_binding_v1(
                  expected_pretag, binding_signature, &expected_binding,
                  &expected_hash) == flynes::session::wire::Status::Ok,
          "test reproduces the canonical 0x0212 binding through the wire codec");
    check(flynes::session::wire::domain_hash(
              kSessionSigningBindingHashDomainV1, expected_binding.data(),
              expected_binding.size()) == expected_hash,
          "the 0x0212 object hash is the domain hash over the exact binding");

    const auto secure_writes = fixture.secure_store.writes;
    const auto object_puts = fixture.object_store.puts;
    const auto key_generates = fixture.key.generates;
    const auto quic_writes = fixture.quic.writes;
    const auto fragments = fixture.discovery.written_fragments.size();

    // Step 1: the SecureStore revision alone must not release the local
    // material; it only releases the next persistence gate.
    deliver_provider_resource(
        fixture.secure_store.inbox, fixture.secure_store.last_token,
        FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2, 319);
    fixture.executor.run_all();
    const auto after_revision = fixture.snapshot();
    check(after_revision.link_state == FLY_SESSION_LINK_CONNECTING_V2 &&
              fixture.object_store.puts == object_puts + 1 &&
              fixture.object_store.last_kind ==
                  flynes::session::wire::kSessionSigningBindingObjectKindV1 &&
              fixture.object_store.last_value.size() == 312 &&
              fixture.object_store.last_value.size() ==
                  flynes::session::wire::kSessionSigningBindingSizeV1 &&
              fixture.object_store.last_hash == expected_hash &&
              std::equal(expected_binding.begin(), expected_binding.end(),
                         fixture.object_store.last_value.begin()),
          "the durable SecureStore revision is only the first gate: it still "
          "requires the exact 0x0212 binding object and leaves the link "
          "CONNECTING");

    if (tail == SessionSigningTail::WrongObjectHash)
    {
        std::array<std::uint8_t, 32> wrong_hash = expected_hash;
        wrong_hash[0] = static_cast<std::uint8_t>(wrong_hash[0] ^ 0xffu);
        const auto result = deliver_provider_hash_raw(
            fixture.object_store.inbox, fixture.object_store.last_token,
            FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, 320, wrong_hash, 6);
        fixture.executor.run_all();
        const auto state = fixture.snapshot();
        check(result == FLY_SESSION_V2_ACCEPTED &&
                  state.link_state == FLY_SESSION_LINK_FAILED_V2 &&
                  state.link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
                  fixture.object_store.puts == object_puts + 1 &&
                  fixture.secure_store.writes == secure_writes &&
                  fixture.key.generates == key_generates &&
                  fixture.quic.writes == quic_writes &&
                  fixture.object_store.cancels == 0 &&
                  fixture.secure_store.cancels == 0,
              "a mismatched 0x0212 object hash fails closed without LINK_HELLO "
              "or a connected lobby");
        const auto late = deliver_provider_hash_raw(
            fixture.object_store.inbox, fixture.object_store.last_token,
            FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, 321, expected_hash, 7);
        fixture.executor.run_all();
        check(late == FLY_SESSION_V2_STALE &&
                  fixture.snapshot().link_state == FLY_SESSION_LINK_FAILED_V2 &&
                  fixture.object_store.puts == object_puts + 1,
              "a late correct object completion cannot revive the failed link");
        return;
    }

    if (tail == SessionSigningTail::ShutdownCancelsThroughObjectStore)
    {
        check(fly_session_begin_shutdown_v2(fixture.engine, 320) ==
                  FLY_SESSION_V2_ACCEPTED &&
                  fixture.object_store.cancels == 1 &&
                  fixture.secure_store.cancels == 0 &&
                  fixture.key.cancels == 0 &&
                  fixture.object_store.puts == object_puts + 1 &&
                  fixture.object_store.last_kind ==
                      flynes::session::wire::kSessionSigningBindingObjectKindV1,
              "an in-flight 0x0212 object request is cancelled through the "
              "ObjectStore port only, never Key or SecureStore");
        fixture.executor.run_all();
        check(fly_session_destroy_v2(fixture.engine) == FLY_SESSION_V2_OK,
              "destroy succeeds after the object-store cancellation terminal");
        fixture.engine = nullptr;
        return;
    }

    // Step 3a: completions that the public ABI must reject without touching the
    // pending persistence state. None of them may emit LINK_HELLO or reach a
    // connected lobby, and none may cancel through another port.
    const auto rejects_without_progress = [&](fly_session_result_v2 result,
                                              const char* message) {
        fixture.executor.run_all();
        const auto state = fixture.snapshot();
        check(result != FLY_SESSION_V2_ACCEPTED &&
                  state.link_state == FLY_SESSION_LINK_CONNECTING_V2 &&
                  state.link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
                  fixture.object_store.puts == object_puts + 1 &&
                  fixture.secure_store.writes == secure_writes &&
                  fixture.key.generates == key_generates &&
                  fixture.quic.writes == quic_writes &&
                  fixture.discovery.written_fragments.size() == fragments &&
                  fixture.object_store.cancels == 0 &&
                  fixture.secure_store.cancels == 0 &&
                  fixture.key.cancels == 0,
              message);
    };

    rejects_without_progress(
        deliver_provider_hash_raw(
            fixture.object_store.inbox, fixture.object_store.last_token,
            FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, 0, expected_hash, 2),
        "a zero 0x0212 object resource handle is rejected without releasing "
        "the persistence gate");

    rejects_without_progress(
        deliver_provider_hash_raw(
            fixture.object_store.inbox, fixture.object_store.last_token,
            FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2, 320, expected_hash,
            3),
        "a wrong completion payload kind cannot complete the 0x0212 object "
        "request");

    auto stale_token = fixture.crypto.last_token;
    rejects_without_progress(
        deliver_provider_hash_raw(
            fixture.object_store.inbox, stale_token,
            FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, 320, expected_hash, 4),
        "an old token cannot complete the current 0x0212 object request");

    auto stale_generation = fixture.object_store.last_token;
    stale_generation.connection_generation =
        fixture.object_store.last_token.connection_generation - 1;
    rejects_without_progress(
        deliver_provider_hash_raw(
            fixture.object_store.inbox, stale_generation,
            FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, 320, expected_hash, 5),
        "an old connection generation cannot complete the current 0x0212 "
        "object request");

    // A store that lost the object between the durable put and the required
    // re-read: the R3 read must then answer FLY_SESSION_V2_UNAVAILABLE, never a
    // fabricated success.
    if (tail == SessionSigningTail::ReadObjectMissing)
        fixture.object_store.stored.clear();

    // Step 2: only the exact immutable-object completion releases the second
    // gate. Local material is then fully durable, so CONNECTING afterwards can
    // only mean the remaining gate is the peer HELLO/READY exchange.
    deliver_provider_hash(
        fixture.object_store.inbox, fixture.object_store.last_token,
        FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, 320, expected_hash);
    fixture.executor.run_all();
    const auto durable = fixture.snapshot();
    // When the store lost the object, the durable re-read already ran inside the
    // same drain and failed the link closed; otherwise both gates are satisfied
    // and the link stays CONNECTING until the peer HELLO/READY exchange.
    const auto state_after_second_gate =
        tail == SessionSigningTail::ReadObjectMissing
            ? FLY_SESSION_LINK_FAILED_V2
            : FLY_SESSION_LINK_CONNECTING_V2;
    check(durable.link_state == state_after_second_gate &&
              durable.link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
              fixture.object_store.puts == object_puts + 1 &&
              fixture.secure_store.writes == secure_writes &&
              fixture.key.generates == key_generates &&
              fixture.quic.writes == quic_writes &&
              fixture.discovery.written_fragments.size() == fragments &&
              fixture.object_store.cancels == 0 &&
              fixture.secure_store.cancels == 0 &&
              fixture.key.cancels == 0,
          "both persistence gates satisfied still leaves CONNECTING until the "
          "peer HELLO/READY exchange");

    /*
     * Decision 1 negative A: the object is absent. The port answers UNAVAILABLE
     * and the engine must fail the link closed - a binding it cannot read back
     * is not durable, and no in-memory copy may stand in for it.
     */
    if (tail == SessionSigningTail::ReadObjectMissing)
    {
        check(fixture.object_store.reads == 1 &&
                  fixture.object_store.missing_reads == 1 &&
                  fixture.object_store.reads == fixture.object_store.missing_reads &&
                  fixture.object_store.last_read_kind ==
                      flynes::session::wire::kSessionSigningBindingObjectKindV1 &&
                  fixture.object_store.last_read_hash == expected_hash,
              "an absent 0x0212 object is reported as UNAVAILABLE by the read "
              "port, never as a successful read");
        const auto state = fixture.snapshot();
        check(state.link_state == FLY_SESSION_LINK_FAILED_V2 &&
                  state.link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
                  fixture.quic.writes == quic_writes &&
                  std::none_of(fixture.object_store.kinds.begin(),
                               fixture.object_store.kinds.end(),
                               [](std::uint32_t kind) {
                                   return kind ==
                                              flynes::session::link::
                                                  kLinkHelloObjectKindV1 ||
                                          kind ==
                                              flynes::session::link::
                                                  kLinkReadyObjectKindV1;
                               }),
              "a failed durable re-read fails the link closed without emitting "
              "LINK_HELLO or reaching a lobby");
        return;
    }

    // Task 9 milestone: at exactly this point start_link_handshake_locked() runs,
    // so the scheduler's first effect (ReadLocalBindingObject) is dispatched and
    // serviced through the R3 object-store read primitive. The engine asks for
    // object kind 0x0212 with the *local binding hash* as the expected content
    // hash, and the mock store really held that object, so this is a genuine
    // durable read-back and not an in-memory assumption.
    check(fixture.object_store.reads == 1 &&
              fixture.object_store.missing_reads == 0 &&
              fixture.object_store.last_read_kind ==
                  flynes::session::wire::kSessionSigningBindingObjectKindV1 &&
              fixture.object_store.last_read_hash == expected_hash &&
              fixture.object_store.read_kinds.size() == 1,
          "the link handshake really re-reads the durable 0x0212 binding "
          "through the R3 object-store read port");

    // Deliver the exact stored bytes and hash. The scheduler verifies them and
    // then stops at the first input this build has no producer for (the
    // contract's u64 channel_id/channel_bind_id, the negotiated result and the
    // peer's accepted 0x0212 binding), which is a not-wired seam: the link must
    // stay CONNECTING, never FAILED and never CONNECTED_LOBBY.
    if (tail == SessionSigningTail::ReadObjectContentMismatch)
    {
        /*
         * Decision 1 negative B: the object exists but the bytes read back do
         * not hash to the expected content hash. The port must not answer
         * UNAVAILABLE for this, and the engine must not treat it as a success:
         * the mismatching 312 bytes fail the link closed.
         */
        auto tampered = expected_binding;
        tampered[180] = static_cast<std::uint8_t>(tampered[180] ^ 0xffu);
        const auto hash_of_tampered = flynes::session::wire::domain_hash(
            kSessionSigningBindingHashDomainV1, tampered.data(), tampered.size());
        deliver_provider_hash_buffer(
            fixture.object_store.read_inbox, fixture.object_store.last_read_token,
            FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, 321, tampered.data(),
            tampered.size(), hash_of_tampered);
        fixture.executor.run_all();
        const auto state = fixture.snapshot();
        check(fixture.object_store.missing_reads == 0 &&
                  fixture.object_store.reads == 1,
              "an existing-but-wrong object is not reported as a missing object");
        check(state.link_state == FLY_SESSION_LINK_FAILED_V2 &&
                  state.link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
                  fixture.quic.writes == quic_writes,
              "a 0x0212 object whose bytes do not match its expected hash fails "
              "the link closed");
        return;
    }

    deliver_provider_hash_buffer(
        fixture.object_store.read_inbox, fixture.object_store.last_read_token,
        FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, 321, expected_binding.data(),
        expected_binding.size(), expected_hash);
    fixture.executor.run_all();
    const auto after_read = fixture.snapshot();
    check(after_read.link_state == FLY_SESSION_LINK_CONNECTING_V2 &&
              after_read.link_state != FLY_SESSION_LINK_FAILED_V2 &&
              after_read.link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
              fixture.object_store.reads == 1 &&
              fixture.object_store.puts == object_puts + 1 &&
              std::none_of(fixture.object_store.kinds.begin(),
                           fixture.object_store.kinds.end(),
                           [](std::uint32_t kind) {
                               return kind ==
                                          flynes::session::link::
                                              kLinkHelloObjectKindV1 ||
                                      kind ==
                                          flynes::session::link::
                                              kLinkReadyObjectKindV1;
                           }),
          "the verified local re-read keeps CONNECTING and persists no "
          "0x0216/0x0217 object while channel_bind_id, the negotiated result and "
          "the peer binding still have no producer");
}

void drive_initiator_pair_flow(EngineFixture& fixture, SessionSigningTail tail)
{
    fixture.platform.ready();
    fixture.executor.run_all();
    std::vector<fly_session_action_descriptor_v2> actions;
    fixture.snapshot(&actions);
    const auto* create = find_action(actions, FLY_SESSION_ACTION_CREATE_INVITE_V2);
    check(create != nullptr, "initiator fixture exposes create invite");
    if (!create) return;
    submit(fixture, *create, 341, false);

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
    payload.resource = 161;
    payload.generation = connection.token.connection_generation;
    payload.value0 = FLY_SESSION_DISCOVERY_PHYSICAL_PERIPHERAL_V2;
    payload.value1 = 23;
    connection.payload_size = sizeof(payload);
    std::memcpy(connection.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(fixture.discovery.inbox, &connection) ==
              FLY_SESSION_V2_ACCEPTED,
          "initiator fixture accepts the provider-owned connection");
    fixture.executor.run_all();
    check(fixture.discovery.subscriptions == 1 &&
              fixture.crypto.randoms == 1 && fixture.crypto.last_size == 48,
          "connected inviter requests exactly 48 PairContext random bytes");
    if (fixture.crypto.randoms != 1)
    {
        fixture.bearer.cancel_result = FLY_SESSION_V2_OK;
        for (auto& action : actions)
            fly_session_approval_token_release_v2(action.approval_token);
        return;
    }

    std::array<std::uint8_t, 48> random{};
    for (std::size_t index = 0; index < random.size(); ++index)
        random[index] = static_cast<std::uint8_t>(index + 1);
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2, random.data(), random.size());
    fixture.executor.run_all();
    check(fixture.discovery.writes == 1,
          "validated PairContext starts one ordered GATT fragment");
    std::size_t completed = 0;
    for (std::size_t guard = 0;
         completed < fixture.discovery.written_fragments.size() && guard < 64;
         ++guard)
    {
        deliver_discovery_end(fixture);
        ++completed;
        fixture.executor.run_all();
    }
    check(completed == fixture.discovery.written_fragments.size() &&
              completed > 1,
          "PairContext waits for every physical write terminal");

    flynes::session::wire::GattReassembler reassembler(
        fixture.discovery.write_token.connection_generation,
        flynes::session::wire::GattPhysicalDirection::PeripheralToCentral);
    std::vector<std::uint8_t> logical;
    auto result = flynes::session::wire::GattFragmentResult::InvalidArgument;
    std::uint64_t now = 1;
    for (const auto& fragment : fixture.discovery.written_fragments)
        result = reassembler.accept(
            fixture.discovery.write_token.connection_generation, now++,
            fragment.data(), fragment.size(), &logical);
    flynes::session::wire::GattLogicalMessageView message{};
    flynes::session::wire::PairContextV1 context{};
    check(result == flynes::session::wire::GattFragmentResult::Complete &&
              flynes::session::wire::decode_gatt_logical_message(
                  logical.data(), logical.size(),
                  static_cast<std::uint8_t>(
                      flynes::session::wire::GattLogicalType::PairContext),
                  &message) ==
                  flynes::session::wire::GattFragmentResult::Complete &&
              flynes::session::wire::decode_pair_context_v1(
                  message.body, message.body_size, &context) ==
                  flynes::session::wire::Status::Ok &&
              std::equal(random.begin(), random.begin() + 16,
                         context.bytes.begin() + 16) &&
              std::equal(random.begin() + 16, random.begin() + 32,
                         context.bytes.begin() + 32) &&
              std::equal(random.begin() + 32, random.end(),
                         context.bytes.begin() + 64),
          "inviter sends one canonical context containing only provider random IDs");

    const auto context_fragment_count = completed;
    deliver_capability(fixture);
    fixture.executor.run_all();
    std::array<std::uint8_t, 32> handle_hash{};
    handle_hash[0] = 1;
    deliver_provider_hash(
        fixture.key.inbox, fixture.key.last_token,
        FLY_SESSION_PROVIDER_KEY_HANDLE_V2, 171, handle_hash);
    fixture.executor.run_all();
    deliver_provider_hash(
        fixture.key.inbox, fixture.key.last_token,
        FLY_SESSION_PROVIDER_KEY_HANDLE_V2, 172, handle_hash);
    fixture.executor.run_all();
    deliver_provider_hash(
        fixture.key.inbox, fixture.key.last_token,
        FLY_SESSION_PROVIDER_KEY_HANDLE_V2, 173, handle_hash);
    fixture.executor.run_all();
    const auto point = p256_generator();
    deliver_provider_buffer(
        fixture.key.inbox, fixture.key.last_token,
        FLY_SESSION_PROVIDER_KEY_PUBLIC_V2, point.data(), point.size());
    fixture.executor.run_all();
    deliver_provider_buffer(
        fixture.key.inbox, fixture.key.last_token,
        FLY_SESSION_PROVIDER_KEY_PUBLIC_V2, point.data(), point.size());
    fixture.executor.run_all();
    const std::array<std::uint8_t, 4> tls_spki{{0x30, 0x02, 0x01, 0x01}};
    deliver_provider_buffer(
        fixture.key.inbox, fixture.key.last_token,
        FLY_SESSION_PROVIDER_KEY_PUBLIC_V2,
        tls_spki.data(), tls_spki.size());
    fixture.executor.run_all();
    const auto tls_hash = flynes::session::wire::sha256(
        tls_spki.data(), tls_spki.size());
    deliver_provider_hash(
        fixture.tls.inbox, fixture.tls.last_token,
        FLY_SESSION_PROVIDER_TLS_MATERIAL_V2, 174, tls_hash);
    fixture.executor.run_all();
    std::array<std::uint8_t, 32> contribution_nonce{};
    contribution_nonce[0] = 0x5a;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2,
        contribution_nonce.data(), contribution_nonce.size());
    fixture.executor.run_all();
    check(fixture.discovery.written_fragments.size() ==
              context_fragment_count + 1,
          "complete initiator material starts its commit after PairContext");
    completed = context_fragment_count;
    for (std::size_t guard = 0;
         completed < fixture.discovery.written_fragments.size() && guard < 128;
         ++guard)
    {
        deliver_discovery_end(fixture);
        ++completed;
        fixture.executor.run_all();
    }
    flynes::session::wire::GattReassembler commit_reassembler(
        fixture.discovery.write_token.connection_generation,
        flynes::session::wire::GattPhysicalDirection::PeripheralToCentral);
    std::vector<std::uint8_t> commit_logical;
    auto commit_result =
        flynes::session::wire::GattFragmentResult::InvalidArgument;
    now = 100;
    for (std::size_t index = context_fragment_count;
         index < fixture.discovery.written_fragments.size(); ++index)
    {
        const auto& fragment = fixture.discovery.written_fragments[index];
        commit_result = commit_reassembler.accept(
            fixture.discovery.write_token.connection_generation, now++,
            fragment.data(), fragment.size(), &commit_logical);
    }
    flynes::session::wire::GattLogicalMessageView commit_message{};
    flynes::session::wire::PairCommitV1 initiator_commit{};
    check(commit_result ==
              flynes::session::wire::GattFragmentResult::Complete &&
              flynes::session::wire::decode_gatt_logical_message(
                  commit_logical.data(), commit_logical.size(),
                  static_cast<std::uint8_t>(
                      flynes::session::wire::GattLogicalType::PairCommit),
                  &commit_message) ==
                  flynes::session::wire::GattFragmentResult::Complete &&
              flynes::session::wire::decode_pair_commit_v1(
                  commit_message.body, commit_message.body_size, context,
                  flynes::session::wire::PairRoleV1::Initiator,
                  flynes::session::wire::validate_p256_uncompressed_point_callback,
                  nullptr, &initiator_commit) ==
                  flynes::session::wire::Status::Ok,
          "inviter sends a canonical initiator commit after local material");

    const auto capability = capability_summary();
    const auto capability_hash = flynes::session::wire::domain_hash(
        "flynes-pair-capability-summary-v1", capability.data(),
        capability.size());
    std::array<std::uint8_t, 32> responder_nonce{};
    responder_nonce[0] = 0x6b;
    std::array<std::uint8_t, 320> responder_contribution_bytes{};
    check(flynes::session::wire::encode_pair_contribution_v1(
              context, flynes::session::wire::PairRoleV1::Responder,
              point, point, tls_hash, responder_nonce, capability_hash,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &responder_contribution_bytes) ==
              flynes::session::wire::Status::Ok,
          "test encodes a canonical responder contribution");
    flynes::session::wire::PairContributionV1 responder_contribution{};
    check(flynes::session::wire::decode_pair_contribution_v1(
              responder_contribution_bytes.data(),
              responder_contribution_bytes.size(), context,
              flynes::session::wire::PairRoleV1::Responder,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &responder_contribution) ==
              flynes::session::wire::Status::Ok,
          "test decodes the canonical responder contribution");
    std::array<std::uint8_t, 152> responder_commit_bytes{};
    check(flynes::session::wire::encode_pair_commit_v1(
              context, responder_contribution,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &responder_commit_bytes) ==
              flynes::session::wire::Status::Ok,
          "test encodes a canonical responder commit");
    deliver_logical(
        fixture,
        logical_v1(static_cast<std::uint8_t>(
                       flynes::session::wire::GattLogicalType::PairCommit),
                   std::vector<std::uint8_t>(responder_commit_bytes.begin(),
                                             responder_commit_bytes.end())),
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::PairCommit),
        32, 2000);
    fixture.executor.run_all();
    check(fixture.key.agreements == 1 && fixture.key.last_resource == 172,
          "responder commit starts initiator ECDH through its PAIR_ECDH key");
    deliver_provider_resource(
        fixture.key.inbox, fixture.key.last_token,
        FLY_SESSION_PROVIDER_KEY_AGREEMENT_V2, 181);
    fixture.executor.run_all();
    deliver_provider_resource(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 182);
    fixture.executor.run_all();
    deliver_provider_resource(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 183);
    fixture.executor.run_all();
    check(fixture.crypto.randoms == 3 && fixture.crypto.last_size == 12,
          "initiator derives both reveal keys before requesting its nonce");
    std::array<std::uint8_t, 12> reveal_nonce{};
    reveal_nonce[0] = 0x7b;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2,
        reveal_nonce.data(), reveal_nonce.size());
    fixture.executor.run_all();
    check(fixture.crypto.aead_seals == 1 &&
              fixture.crypto.last_resource == 182,
          "initiator seals its contribution with the i2r key");
    std::array<std::uint8_t,
               flynes::session::wire::kPairRevealCiphertextAndTagSizeV1>
        initiator_ciphertext{};
    initiator_ciphertext[0] = 0x7c;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        initiator_ciphertext.data(), initiator_ciphertext.size());
    fixture.executor.run_all();
    const auto commit_end = completed;
    check(fixture.discovery.written_fragments.size() == commit_end + 1,
          "sealed initiator reveal starts one ordered GATT fragment");
    for (std::size_t guard = 0;
         completed < fixture.discovery.written_fragments.size() && guard < 128;
         ++guard)
    {
        deliver_discovery_end(fixture);
        ++completed;
        fixture.executor.run_all();
    }

    std::array<std::uint8_t, 12> peer_reveal_nonce{};
    peer_reveal_nonce[0] = 0x7d;
    std::array<std::uint8_t,
               flynes::session::wire::kPairRevealCiphertextAndTagSizeV1>
        peer_ciphertext{};
    peer_ciphertext[0] = 0x7e;
    std::array<std::uint8_t,
               flynes::session::wire::kPairRevealBodySizeV1>
        peer_reveal_body{};
    check(flynes::session::wire::encode_pair_reveal_envelope_v1(
              peer_reveal_nonce, peer_ciphertext, &peer_reveal_body) ==
              flynes::session::wire::Status::Ok,
          "test encodes the responder reveal envelope");
    deliver_logical(
        fixture,
        logical_v1(static_cast<std::uint8_t>(
                       flynes::session::wire::GattLogicalType::PairReveal),
                   std::vector<std::uint8_t>(peer_reveal_body.begin(),
                                             peer_reveal_body.end())),
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::PairReveal),
        33, 3000);
    fixture.executor.run_all();
    check(fixture.crypto.aead_opens == 1 &&
              fixture.crypto.last_resource == 183,
          "initiator opens the responder reveal with the r2i key");
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        responder_contribution_bytes.data(),
        responder_contribution_bytes.size());
    fixture.executor.run_all();
    check(fixture.crypto.hkdfs == 3 && fixture.crypto.last_resource == 181 &&
              std::string(fixture.crypto.last_info.begin(),
                          fixture.crypto.last_info.end()) ==
                  "flynes-pair-control-i2r-v1",
          "verified reveals start control-key derivation from the ECDH secret");
    deliver_provider_resource(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 184);
    fixture.executor.run_all();
    check(fixture.crypto.hkdfs == 4 &&
              std::string(fixture.crypto.last_info.begin(),
                          fixture.crypto.last_info.end()) ==
                  "flynes-pair-control-r2i-v1",
          "public engine derives the role-independent r2i control key second");
    deliver_provider_resource(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 185);
    fixture.executor.run_all();
    check(fixture.key.signs == 1 && fixture.key.last_resource == 171 &&
              std::string(fixture.key.last_domain.begin(),
                          fixture.key.last_domain.end()) ==
                  "flynes-pair-signature-v1",
          "initiator signs the transcript with the identity-purpose handle");
    std::array<std::uint8_t, 64> initiator_signature{};
    initiator_signature[0] = 1;
    initiator_signature[32] = 1;
    deliver_provider_buffer(
        fixture.key.inbox, fixture.key.last_token,
        FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2,
        initiator_signature.data(), initiator_signature.size());
    fixture.executor.run_all();
    check(fixture.crypto.verifies == 1 &&
              fixture.crypto.last_signature == std::vector<std::uint8_t>(
                  initiator_signature.begin(), initiator_signature.end()),
          "locally produced signature must cross public-provider verification");
    const auto transcript_hash = fixture.crypto.last_digest;
    deliver_provider_end(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2);
    fixture.executor.run_all();
    check(fixture.crypto.aead_seals == 2 &&
              fixture.crypto.last_resource == 184 &&
              fixture.crypto.last_input.size() ==
                  flynes::session::wire::kPairSignatureInnerSizeV1,
          "verified initiator signature is sealed with the i2r control key");
    std::array<std::uint8_t, 192> signature_ciphertext{};
    signature_ciphertext[0] = 0x81;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        signature_ciphertext.data(), signature_ciphertext.size());
    fixture.executor.run_all();
    const auto signature_fragment_start = completed;
    check(fixture.discovery.written_fragments.size() ==
              signature_fragment_start + 1,
          "sealed initiator signature starts ordered GATT delivery");
    for (std::size_t guard = 0;
         completed < fixture.discovery.written_fragments.size() && guard < 128;
         ++guard)
    {
        deliver_discovery_end(fixture);
        ++completed;
        fixture.executor.run_all();
    }

    std::array<std::uint8_t, 64> responder_signature{};
    responder_signature[0] = 2;
    responder_signature[32] = 2;
    std::array<std::uint8_t,
               flynes::session::wire::kPairSignatureInnerSizeV1>
        responder_signature_inner{};
    check(flynes::session::wire::encode_pair_signature_inner_v1(
              transcript_hash,
              flynes::session::wire::PairRoleV1::Responder,
              point, point, responder_signature,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &responder_signature_inner) ==
              flynes::session::wire::Status::Ok,
          "test encodes an exact responder signature inner");
    std::array<std::uint8_t, 192> responder_signature_ciphertext{};
    responder_signature_ciphertext[0] = 0x82;
    std::vector<std::uint8_t> responder_signature_envelope;
    check(flynes::session::wire::encode_pair_secure_envelope_v1(
              static_cast<std::uint8_t>(
                  flynes::session::wire::GattLogicalType::PairSignature),
              1, responder_signature_ciphertext.data(),
              responder_signature_ciphertext.size(),
              &responder_signature_envelope) ==
              flynes::session::wire::Status::Ok,
          "test seals the responder signature wire envelope shape");
    deliver_logical(
        fixture,
        logical_v1(static_cast<std::uint8_t>(
                       flynes::session::wire::GattLogicalType::PairSignature),
                   responder_signature_envelope),
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::PairSignature),
        34, 4000);
    fixture.executor.run_all();
    check(fixture.crypto.aead_opens == 2 &&
              fixture.crypto.last_resource == 185,
          "initiator opens the responder signature with the r2i control key");
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        responder_signature_inner.data(), responder_signature_inner.size());
    fixture.executor.run_all();
    check(fixture.crypto.verifies == 2 &&
              fixture.crypto.last_signature == std::vector<std::uint8_t>(
                  responder_signature.begin(), responder_signature.end()),
          "decoded responder signature reaches exact public verification");
    deliver_provider_end(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2);
    fixture.executor.run_all();
    check(fixture.object_store.puts == 1 &&
              fixture.object_store.last_kind ==
                  flynes::session::wire::kPairTranscriptObjectKindV1 &&
              fixture.object_store.last_value.size() ==
                  flynes::session::wire::kPairTranscriptSizeV1 &&
              std::equal(initiator_signature.begin(), initiator_signature.end(),
                         fixture.object_store.last_value.begin() + 752) &&
              std::equal(responder_signature.begin(), responder_signature.end(),
                         fixture.object_store.last_value.begin() + 816) &&
              fixture.crypto.hkdfs == 4,
          "public engine durably stores the exact PairTranscript before SAS");
    deliver_provider_hash(
        fixture.object_store.inbox, fixture.object_store.last_token,
        FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, 186,
        fixture.object_store.last_hash);
    fixture.executor.run_all();
    check(fixture.crypto.hkdfs == 5 && fixture.crypto.last_resource == 181 &&
              std::string(fixture.crypto.last_info.begin(),
                          fixture.crypto.last_info.end()) ==
                  "flynes-pair-gatt-i2r-v1",
          "both verified signatures begin SAS purpose-key derivation");
    deliver_provider_resource(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 186);
    fixture.executor.run_all();
    deliver_provider_resource(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 187);
    fixture.executor.run_all();
    deliver_provider_resource(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 188);
    fixture.executor.run_all();
    check(fixture.crypto.hmacs == 1 && fixture.crypto.last_resource == 188 &&
              fixture.crypto.last_input == std::vector<std::uint8_t>(
                  transcript_hash.begin(), transcript_hash.end()),
          "SAS is derived only through provider HMAC over the transcript");
    std::array<std::uint8_t, 32> sas_block{};
    sas_block[3] = 42;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        sas_block.data(), sas_block.size());
    fixture.executor.run_all();

    check(fixture.crypto.hmacs == 2 && fixture.crypto.last_resource == 186,
          "verified pair signatures always begin private KNOWN_STATUS exchange");
    std::array<std::uint8_t, 32> local_known_tag{};
    local_known_tag[0] = 0x85;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        local_known_tag.data(), local_known_tag.size());
    fixture.executor.run_all();
    check(fixture.crypto.aead_seals == 3 &&
              fixture.crypto.last_input.size() ==
                  flynes::session::wire::kKnownStatusInnerSizeV1,
          "anonymous candidate emits equal-size type21 known status");
    std::vector<std::uint8_t> local_known_ciphertext = fixture.crypto.last_input;
    local_known_ciphertext.insert(local_known_ciphertext.end(), 16, 0x86);
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        local_known_ciphertext.data(), local_known_ciphertext.size());
    fixture.executor.run_all();
    for (std::size_t guard = 0;
         completed < fixture.discovery.written_fragments.size() && guard < 128;
         ++guard)
    {
        deliver_discovery_end(fixture);
        ++completed;
        fixture.executor.run_all();
    }

    std::array<std::uint8_t, 32> peer_known_tag{};
    peer_known_tag[0] = 0x87;
    std::array<std::uint8_t,
               flynes::session::wire::kKnownStatusInnerSizeV1>
        peer_known_inner{};
    check(flynes::session::wire::encode_known_status_inner_v1(
              transcript_hash, flynes::session::wire::PairRoleV1::Responder,
              false, point, point, peer_known_tag,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &peer_known_inner) == flynes::session::wire::Status::Ok,
          "test encodes exact responder KNOWN_STATUS");
    std::array<std::uint8_t, 160> peer_known_ciphertext{};
    peer_known_ciphertext[0] = 0x88;
    std::vector<std::uint8_t> peer_known_envelope;
    check(flynes::session::wire::encode_pair_secure_envelope_v1(
              static_cast<std::uint8_t>(
                  flynes::session::wire::GattLogicalType::PairKnownStatus),
              2, peer_known_ciphertext.data(), peer_known_ciphertext.size(),
              &peer_known_envelope) == flynes::session::wire::Status::Ok,
          "test encodes responder KNOWN_STATUS at route counter two");
    deliver_logical(
        fixture,
        logical_v1(static_cast<std::uint8_t>(
                       flynes::session::wire::GattLogicalType::PairKnownStatus),
                   peer_known_envelope),
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::PairKnownStatus),
        35, 4500);
    fixture.executor.run_all();
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        peer_known_inner.data(), peer_known_inner.size());
    fixture.executor.run_all();
    check(fixture.crypto.hmacs == 3 && fixture.crypto.last_resource == 187,
          "peer KNOWN_STATUS tag is verified with the responder GATT key");
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        peer_known_tag.data(), peer_known_tag.size());
    fixture.executor.run_all();
    check(fixture.crypto.aead_seals == 4 &&
              fixture.crypto.last_input.size() ==
                  flynes::session::wire::kKnownBranchInnerSizeV1 &&
              fixture.crypto.last_input[42] == static_cast<std::uint8_t>(
                  flynes::session::wire::KnownBranchKindV1::SasFallback),
          "mixed-or-unknown contact state always emits zero-proof SAS fallback");
    std::vector<std::uint8_t> local_branch_ciphertext = fixture.crypto.last_input;
    local_branch_ciphertext.insert(local_branch_ciphertext.end(), 16, 0x89);
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        local_branch_ciphertext.data(), local_branch_ciphertext.size());
    fixture.executor.run_all();
    for (std::size_t guard = 0;
         completed < fixture.discovery.written_fragments.size() && guard < 128;
         ++guard)
    {
        deliver_discovery_end(fixture);
        ++completed;
        fixture.executor.run_all();
    }

    std::array<std::uint8_t, 64> zero_branch_proof{};
    std::array<std::uint8_t,
               flynes::session::wire::kKnownBranchInnerSizeV1>
        peer_branch_inner{};
    check(flynes::session::wire::encode_known_branch_inner_v1(
              transcript_hash, flynes::session::wire::PairRoleV1::Responder,
              flynes::session::wire::KnownBranchKindV1::SasFallback,
              point, point, zero_branch_proof,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &peer_branch_inner) == flynes::session::wire::Status::Ok,
          "test encodes exact responder zero-proof KNOWN_BRANCH");
    std::array<std::uint8_t, 192> peer_branch_ciphertext{};
    peer_branch_ciphertext[0] = 0x8a;
    std::vector<std::uint8_t> peer_branch_envelope;
    check(flynes::session::wire::encode_pair_secure_envelope_v1(
              static_cast<std::uint8_t>(
                  flynes::session::wire::GattLogicalType::PairKnownBranch),
              3, peer_branch_ciphertext.data(), peer_branch_ciphertext.size(),
              &peer_branch_envelope) == flynes::session::wire::Status::Ok,
          "test encodes responder KNOWN_BRANCH at route counter three");
    deliver_logical(
        fixture,
        logical_v1(static_cast<std::uint8_t>(
                       flynes::session::wire::GattLogicalType::PairKnownBranch),
                   peer_branch_envelope),
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::PairKnownBranch),
        36, 4750);
    fixture.executor.run_all();
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        peer_branch_inner.data(), peer_branch_inner.size());
    fixture.executor.run_all();
    std::vector<fly_session_action_descriptor_v2> sas_actions;
    check(fixture.snapshot(&sas_actions).link_state ==
              FLY_SESSION_LINK_AUTHENTICATING_V2 &&
              find_action(sas_actions, FLY_SESSION_ACTION_CONFIRM_SAS_V2) &&
              find_action(sas_actions, FLY_SESSION_ACTION_REJECT_SAS_V2),
          "SAS readiness exposes confirm and reject without overclaiming a link");
    const auto pairing = fixture.pairing();
    check(std::equal(std::begin(pairing.sas), std::end(pairing.sas),
                     std::array<std::uint8_t, 6>{{'0','0','0','0','4','2'}}.begin()) &&
              pairing.stage == FLY_SESSION_PAIRING_AWAITING_LOCAL_SAS_V2 &&
              pairing.local_confirmed == 0 && pairing.peer_confirmed == 0 &&
              std::equal(std::begin(pairing.pair_transcript_hash),
                         std::end(pairing.pair_transcript_hash),
                         transcript_hash.begin()),
          "pairing subview exposes exact SAS and transcript-bound confirmation bits");
    const auto* confirm = find_action(
        sas_actions, FLY_SESSION_ACTION_CONFIRM_SAS_V2);
    if (confirm)
        submit(fixture, *confirm, 342, false);
    std::vector<fly_session_action_descriptor_v2> after_confirm_actions;
    check(fixture.snapshot(&after_confirm_actions).link_state ==
              FLY_SESSION_LINK_AUTHENTICATING_V2 &&
              !find_action(after_confirm_actions,
                           FLY_SESSION_ACTION_CONFIRM_SAS_V2),
          "single-side SAS confirmation remains authenticating and is one-shot");
    const auto confirmed_pairing = fixture.pairing();
    check(confirmed_pairing.stage ==
              FLY_SESSION_PAIRING_AWAITING_PEER_CONFIRM_V2 &&
              confirmed_pairing.local_confirmed == 1 &&
              confirmed_pairing.peer_confirmed == 0,
          "pairing subview reports local-only confirmation without upgrading link");
    check(fixture.crypto.hmacs == 4 && fixture.crypto.last_resource == 186 &&
              fixture.crypto.last_input.size() == 137,
          "local approval starts exact initiator KEY_CONFIRM HMAC");
    std::array<std::uint8_t, 32> initiator_confirm_tag{};
    initiator_confirm_tag[0] = 0x91;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        initiator_confirm_tag.data(), initiator_confirm_tag.size());
    fixture.executor.run_all();
    check(fixture.crypto.hmacs == 5 && fixture.crypto.last_resource == 186,
          "local KEY_CONFIRM tag is reverified by the public auth reducer");
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        initiator_confirm_tag.data(), initiator_confirm_tag.size());
    fixture.executor.run_all();
    check(fixture.crypto.aead_seals == 5 &&
              fixture.crypto.last_resource == 184 &&
              fixture.crypto.last_input.size() ==
                  flynes::session::wire::kKeyConfirmInnerSizeV1,
          "verified local KEY_CONFIRM is sealed with the i2r control key");
    std::array<std::uint8_t, 160> initiator_confirm_ciphertext{};
    initiator_confirm_ciphertext[0] = 0x92;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        initiator_confirm_ciphertext.data(),
        initiator_confirm_ciphertext.size());
    fixture.executor.run_all();
    for (std::size_t guard = 0;
         completed < fixture.discovery.written_fragments.size() && guard < 128;
         ++guard)
    {
        deliver_discovery_end(fixture);
        ++completed;
        fixture.executor.run_all();
    }

    std::array<std::uint8_t, 32> responder_confirm_tag{};
    responder_confirm_tag[0] = 0x93;
    std::array<std::uint8_t,
               flynes::session::wire::kKeyConfirmInnerSizeV1>
        responder_confirm_inner{};
    check(flynes::session::wire::encode_key_confirm_inner_v1(
              1, FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2, transcript_hash,
              flynes::session::wire::PairRoleV1::Responder,
              point, point, responder_confirm_tag,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &responder_confirm_inner) ==
              flynes::session::wire::Status::Ok,
          "test encodes exact responder KEY_CONFIRM inner");
    std::array<std::uint8_t, 160> responder_confirm_ciphertext{};
    responder_confirm_ciphertext[0] = 0x94;
    std::vector<std::uint8_t> responder_confirm_envelope;
    check(flynes::session::wire::encode_pair_secure_envelope_v1(
              static_cast<std::uint8_t>(
                  flynes::session::wire::GattLogicalType::KeyConfirm),
              4, responder_confirm_ciphertext.data(),
              responder_confirm_ciphertext.size(),
              &responder_confirm_envelope) ==
              flynes::session::wire::Status::Ok,
          "test encodes responder KEY_CONFIRM envelope at counter four");
    deliver_logical(
        fixture,
        logical_v1(static_cast<std::uint8_t>(
                       flynes::session::wire::GattLogicalType::KeyConfirm),
                   responder_confirm_envelope),
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::KeyConfirm),
        37, 5000);
    fixture.executor.run_all();
    check(fixture.crypto.aead_opens == 5 &&
              fixture.crypto.last_resource == 185,
          "initiator opens responder KEY_CONFIRM with r2i control key");
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        responder_confirm_inner.data(), responder_confirm_inner.size());
    fixture.executor.run_all();
    check(fixture.crypto.hmacs == 6 && fixture.crypto.last_resource == 187,
          "peer KEY_CONFIRM tag is verified with r2i GATT key");
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        responder_confirm_tag.data(), responder_confirm_tag.size());
    fixture.executor.run_all();
    const auto fully_confirmed = fixture.pairing();
    check(fixture.snapshot().link_state == FLY_SESSION_LINK_PROVISIONING_V2 &&
              fully_confirmed.stage == FLY_SESSION_PAIRING_CONFIRMED_V2 &&
              fully_confirmed.local_confirmed == 1 &&
              fully_confirmed.peer_confirmed == 1,
          "both KEY_CONFIRM receipts enter provisioning, never connected lobby");
    check(fixture.crypto.hmacs == 7 && fixture.crypto.last_resource == 186 &&
              fixture.crypto.last_input.size() == 596,
          "mutual KEY_CONFIRM starts authenticated local capability HMAC");
    std::array<std::uint8_t, 32> local_capability_tag{};
    local_capability_tag[0] = 0xa1;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        local_capability_tag.data(), local_capability_tag.size());
    fixture.executor.run_all();
    check(fixture.crypto.aead_seals == 6 &&
              fixture.crypto.last_resource == 184 &&
              fixture.crypto.last_input.size() ==
                  flynes::session::wire::kPairCapabilityInnerSizeV1,
          "local capability reveal is sealed at PairSecure counter five");
    std::array<std::uint8_t, 608> local_capability_ciphertext{};
    local_capability_ciphertext[0] = 0xa2;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        local_capability_ciphertext.data(),
        local_capability_ciphertext.size());
    fixture.executor.run_all();
    for (std::size_t guard = 0;
         completed < fixture.discovery.written_fragments.size() && guard < 256;
         ++guard)
    {
        deliver_discovery_end(fixture);
        ++completed;
        fixture.executor.run_all();
    }

    std::array<std::uint8_t, 32> peer_capability_tag{};
    peer_capability_tag[0] = 0xa3;
    std::array<std::uint8_t,
               flynes::session::wire::kPairCapabilityInnerSizeV1>
        peer_capability_inner{};
    check(flynes::session::wire::encode_pair_capability_inner_v1(
              transcript_hash,
              flynes::session::wire::PairRoleV1::Responder,
              capability, peer_capability_tag, &peer_capability_inner) ==
              flynes::session::wire::Status::Ok,
          "test encodes authenticated responder capability summary");
    std::array<std::uint8_t, 608> peer_capability_ciphertext{};
    peer_capability_ciphertext[0] = 0xa4;
    std::vector<std::uint8_t> peer_capability_envelope;
    check(flynes::session::wire::encode_pair_secure_envelope_v1(
              static_cast<std::uint8_t>(
                  flynes::session::wire::GattLogicalType::PairCapabilityReveal),
              5, peer_capability_ciphertext.data(),
              peer_capability_ciphertext.size(),
              &peer_capability_envelope) == flynes::session::wire::Status::Ok,
          "test encodes responder capability PairSecure envelope");
    deliver_logical(
        fixture,
        logical_v1(static_cast<std::uint8_t>(
                       flynes::session::wire::GattLogicalType::PairCapabilityReveal),
                   peer_capability_envelope),
        static_cast<std::uint8_t>(
            flynes::session::wire::GattLogicalType::PairCapabilityReveal),
        38, 6000);
    fixture.executor.run_all();
    check(fixture.crypto.aead_opens == 6 &&
              fixture.crypto.last_resource == 185,
          "peer capability opens only with the r2i control key");
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        peer_capability_inner.data(), peer_capability_inner.size());
    fixture.executor.run_all();
    check(fixture.crypto.hmacs == 8 && fixture.crypto.last_resource == 187,
          "peer capability pretag is verified with r2i GATT key");
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        peer_capability_tag.data(), peer_capability_tag.size());
    fixture.executor.run_all();
    check(fixture.snapshot().link_state == FLY_SESSION_LINK_PROVISIONING_V2,
          "authenticated matching capabilities select a plan without connecting");

    std::array<std::uint8_t, 16> plan_nonce{};
    plan_nonce[0] = 0xb1;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2,
        plan_nonce.data(), plan_nonce.size());
    fixture.executor.run_all();
    check(fixture.crypto.hmacs == 9 && fixture.crypto.last_resource == 186,
          "selected plan starts initiator direction HMAC after provider random");
    std::array<std::uint8_t, 32> plan_tag{};
    plan_tag[0] = 0xb2;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        plan_tag.data(), plan_tag.size());
    fixture.executor.run_all();
    check(fixture.crypto.aead_seals == 7 &&
              fixture.crypto.last_input.size() ==
                  flynes::session::wire::kInitialBearerPlanInnerSizeV1,
          "type24 exact inner is sealed at PairSecure initiator counter six");
    std::array<std::uint8_t, 224> plan_ciphertext{};
    plan_ciphertext[0] = 0xb3;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        plan_ciphertext.data(), plan_ciphertext.size());
    fixture.executor.run_all();
    check(fixture.secure_store.writes == 1 &&
              fixture.discovery.written_fragments.size() == completed,
          "type24 is durably persisted before any ordered GATT fragment");
    deliver_provider_resource(
        fixture.secure_store.inbox, fixture.secure_store.last_token,
        FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2, 301);
    fixture.executor.run_all();
    const auto plan_fragment_start = completed;
    for (std::size_t guard = 0;
         completed < fixture.discovery.written_fragments.size() && guard < 128;
         ++guard)
    {
        deliver_discovery_end(fixture);
        ++completed;
        fixture.executor.run_all();
    }
    flynes::session::wire::GattReassembler plan_reassembler(
        fixture.discovery.write_token.connection_generation,
        flynes::session::wire::GattPhysicalDirection::PeripheralToCentral);
    std::vector<std::uint8_t> plan_logical;
    auto plan_result = flynes::session::wire::GattFragmentResult::InvalidArgument;
    for (std::size_t index = plan_fragment_start; index < completed; ++index)
    {
        const auto& fragment = fixture.discovery.written_fragments[index];
        plan_result = plan_reassembler.accept(
            fixture.discovery.write_token.connection_generation,
            7000 + index, fragment.data(), fragment.size(), &plan_logical);
    }
    check(plan_result == flynes::session::wire::GattFragmentResult::Complete &&
              plan_logical.size() == 276 && plan_logical[1] == 24,
          "persisted type24 bytes are reused exactly for GATT delivery");
    std::array<std::uint8_t, 32> plan_logical_hash{};
    std::copy(plan_logical.end() - 32, plan_logical.end(),
              plan_logical_hash.begin());

    flynes::session::wire::BearerPlanBytes selected_plan_bytes{};
    std::copy_n(capability.begin() + 96, selected_plan_bytes.size(),
                selected_plan_bytes.begin());
    const auto selected_plan_hash =
        flynes::session::wire::selected_bearer_plan_hash_v1(
            selected_plan_bytes);
    std::array<std::uint8_t, 32> ack_tag{};
    ack_tag[0] = 0xb4;
    std::array<std::uint8_t,
               flynes::session::wire::kInitialBearerPlanAckInnerSizeV1>
        ack_inner{};
    check(flynes::session::wire::encode_initial_bearer_plan_ack_inner_v1(
              transcript_hash, plan_logical_hash, selected_plan_hash,
              ack_tag, &ack_inner) == flynes::session::wire::Status::Ok,
          "test encodes exact type25 acknowledgement");
    std::array<std::uint8_t, 160> ack_ciphertext{};
    ack_ciphertext[0] = 0xb5;
    std::vector<std::uint8_t> ack_envelope;
    check(flynes::session::wire::encode_pair_secure_envelope_v1(
              25, 6, ack_ciphertext.data(), ack_ciphertext.size(),
              &ack_envelope) == flynes::session::wire::Status::Ok,
          "test wraps type25 at responder counter six");
    const auto ack_logical = logical_v1(25, ack_envelope);
    deliver_logical(fixture, ack_logical, 25, 39, 7000);
    fixture.executor.run_all();
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        ack_inner.data(), ack_inner.size());
    fixture.executor.run_all();
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        ack_tag.data(), ack_tag.size());
    fixture.executor.run_all();
    check(fixture.secure_store.writes == 2,
          "initiator persists verified type25 and its local lock atomically");
    deliver_provider_resource(
        fixture.secure_store.inbox, fixture.secure_store.last_token,
        FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2, 302);
    fixture.executor.run_all();
    check(fixture.crypto.hmacs == 11,
          "persisted type25 authorizes exact type26 HMAC construction");
    std::array<std::uint8_t, 32> final_tag{};
    final_tag[0] = 0xb6;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        final_tag.data(), final_tag.size());
    fixture.executor.run_all();
    check(fixture.crypto.last_input.size() ==
              flynes::session::wire::kInitialBearerPlanFinalInnerSizeV1,
          "type26 binds both complete type24/type25 logical hashes");
    std::array<std::uint8_t, 192> final_ciphertext{};
    final_ciphertext[0] = 0xb7;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        final_ciphertext.data(), final_ciphertext.size());
    fixture.executor.run_all();
    check(fixture.secure_store.writes == 3 &&
              fixture.discovery.written_fragments.size() == completed,
          "type26 is persisted before its first GATT fragment");
    deliver_provider_resource(
        fixture.secure_store.inbox, fixture.secure_store.last_token,
        FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2, 303);
    fixture.executor.run_all();
    for (std::size_t guard = 0;
         completed < fixture.discovery.written_fragments.size() && guard < 128;
         ++guard)
    {
        deliver_discovery_end(fixture);
        ++completed;
        fixture.executor.run_all();
    }
    check(fixture.secure_store.writes == 4,
          "successful current-generation type26 send requests mutual-lock persistence");
    deliver_provider_resource(
        fixture.secure_store.inbox, fixture.secure_store.last_token,
        FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2, 304);
    fixture.executor.run_all();
    check(fixture.snapshot().link_state == FLY_SESSION_LINK_PROVISIONING_V2 &&
              fixture.secure_store.writes == 4 &&
              fixture.crypto.last_info == std::vector<std::uint8_t>(
                  {'f','l','y','n','e','s','-','i','n','i','t','i','a','l','-',
                   'b','e','a','r','e','r','-','c','r','e','d','e','n','t','i',
                   'a','l','-','i','2','r','-','v','1'}),
          "mutual lock starts the dedicated i2r credential key derivation");
    deliver_provider_resource(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 305);
    fixture.executor.run_all();
    check(fixture.crypto.last_info == std::vector<std::uint8_t>(
              {'f','l','y','n','e','s','-','i','n','i','t','i','a','l','-',
               'b','e','a','r','e','r','-','c','r','e','d','e','n','t','i',
               'a','l','-','r','2','i','-','v','1'}),
          "credential keys use distinct exact directional labels");
    deliver_provider_resource(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 306);
    fixture.executor.run_all();
    check(fixture.bearer.creates == 1 && fixture.bearer.joins == 0 &&
              fixture.bearer.last_resource == 0 &&
              fixture.bearer.last_confirmation_budget == 0,
          "only the selected creator starts bearer with the locked prompt budget");
    deliver_provider_resource(
        fixture.bearer.inbox, fixture.bearer.last_token,
        FLY_SESSION_PROVIDER_BEARER_PATH_V2, 307);
    fixture.executor.run_all();
    check(fixture.bearer.prepares == 1 && fixture.bearer.last_creator &&
              fixture.bearer.last_resource == 307 &&
              fixture.bearer.last_plan.size() == 48,
          "created path is converted to canonical join parameters");
    std::array<std::uint8_t, 100> bearer_credential{};
    bearer_credential[0] = 1;
    bearer_credential[1] = 1;
    bearer_credential[2] = 16;
    std::fill_n(bearer_credential.begin() + 4, 16, std::uint8_t{0xc1});
    std::array<std::uint8_t,
               flynes::session::wire::kBearerJoinParamsSizeV1> join_params{};
    check(flynes::session::wire::encode_bearer_join_params_v1(
              selected_plan_bytes, 60000, bearer_credential, &join_params) ==
              flynes::session::wire::Status::Ok,
          "test prepares canonical initial bearer join parameters");
    const auto join_hash = flynes::session::wire::sha256(
        join_params.data(), join_params.size());
    deliver_provider_hash_buffer(
        fixture.bearer.inbox, fixture.bearer.last_token,
        FLY_SESSION_PROVIDER_BEARER_CREDENTIAL_V2, 308,
        join_params.data(), join_params.size(), join_hash);
    fixture.executor.run_all();
    check(fixture.crypto.aead_seals == 9 &&
              fixture.crypto.last_resource == 305 &&
              fixture.crypto.last_input.size() ==
                  flynes::session::wire::kInitialBearerCredentialPlaintextSizeV1,
          "canonical type8 plaintext is sealed with the creator direction key");
    std::vector<std::uint8_t> credential_ciphertext = fixture.crypto.last_input;
    credential_ciphertext.insert(credential_ciphertext.end(), 16, 0xd1);
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        credential_ciphertext.data(), credential_ciphertext.size());
    fixture.executor.run_all();
    check(fixture.secure_store.writes == 5 &&
              fixture.secure_store.last_value.size() == 668,
          "complete exact type8 logical message persists before publication");
    deliver_provider_resource(
        fixture.secure_store.inbox, fixture.secure_store.last_token,
        FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2, 309);
    fixture.executor.run_all();
    for (std::size_t guard = 0;
         completed < fixture.discovery.written_fragments.size() && guard < 128;
         ++guard)
    {
        deliver_discovery_end(fixture);
        ++completed;
        fixture.executor.run_all();
    }
    check(fixture.snapshot().link_state == FLY_SESSION_LINK_CONNECTING_V2 &&
              fixture.bearer.creates == 1 && fixture.bearer.joins == 0,
          "public creator reaches CONNECTING only after persisted type8 send");
    deliver_provider_resource(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 310);
    fixture.executor.run_all();
    deliver_provider_resource(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 311);
    fixture.executor.run_all();
    check(fixture.bearer.resolves == 1 && fixture.bearer.last_resource == 307 &&
              fixture.bearer.last_join_params.empty(),
          "numeric listener resolves endpoint only on the authenticated bearer path");
    std::array<std::uint8_t, 18> endpoint{};
    endpoint[12] = 192; endpoint[13] = 168; endpoint[14] = 1; endpoint[15] = 9;
    endpoint[16] = 0xd6; endpoint[17] = 0xd8;
    deliver_provider_buffer(
        fixture.bearer.inbox, fixture.bearer.last_token,
        FLY_SESSION_PROVIDER_BEARER_ENDPOINT_V2,
        endpoint.data(), endpoint.size());
    fixture.executor.run_all();
    check(fixture.crypto.last_size == 12,
          "resolved endpoint requests a fresh public offer nonce");
    std::array<std::uint8_t, 12> endpoint_nonce{};
    endpoint_nonce[0] = 0xe1;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2,
        endpoint_nonce.data(), endpoint_nonce.size());
    fixture.executor.run_all();
    check(fixture.crypto.aead_seals == 10 &&
              fixture.crypto.last_resource == 310 &&
              fixture.crypto.last_input.size() ==
                  flynes::session::wire::kEndpointOfferPlaintextSizeV1,
          "type22 seals exact endpoint binding with listener direction key");
    std::vector<std::uint8_t> endpoint_ciphertext = fixture.crypto.last_input;
    endpoint_ciphertext.insert(endpoint_ciphertext.end(), 16, 0xe2);
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        endpoint_ciphertext.data(), endpoint_ciphertext.size());
    fixture.executor.run_all();
    check(fixture.secure_store.writes == 6 &&
              fixture.secure_store.last_value.size() == 256,
          "complete type22 logical bytes persist before GATT publication");
    deliver_provider_resource(
        fixture.secure_store.inbox, fixture.secure_store.last_token,
        FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2, 312);
    fixture.executor.run_all();
    for (std::size_t guard = 0;
         completed < fixture.discovery.written_fragments.size() && guard < 128;
         ++guard)
    {
        deliver_discovery_end(fixture);
        ++completed;
        fixture.executor.run_all();
    }
    check(fixture.snapshot().link_state == FLY_SESSION_LINK_CONNECTING_V2,
          "persisted endpoint offer remains connecting until QUIC is authenticated");

    check(fixture.crypto.last_info == std::vector<std::uint8_t>(
              {'f','l','y','n','e','s','-','p','a','i','r','-','q','u','i','c','-',
               'b','i','n','d','-','i','2','r','-','v','1'}),
          "endpoint delivery starts the exact i2r ChannelBind key derivation");
    deliver_provider_resource(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 313);
    fixture.executor.run_all();
    check(fixture.crypto.last_info == std::vector<std::uint8_t>(
              {'f','l','y','n','e','s','-','p','a','i','r','-','q','u','i','c','-',
               'b','i','n','d','-','r','2','i','-','v','1'}),
          "ChannelBind derives the opposite direction key second");
    deliver_provider_resource(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 314);
    fixture.executor.run_all();
    check(fixture.quic.listens == 1 && fixture.quic.connects == 0 &&
              fixture.quic.last_resource == 307 &&
              fixture.quic.last_endpoint ==
                  std::vector<std::uint8_t>(endpoint.begin(), endpoint.end()),
          "selected local listener starts pinned QUIC on the certified bearer");

    deliver_provider_resource(
        fixture.quic.inbox, fixture.quic.last_token,
        FLY_SESSION_PROVIDER_QUIC_CONNECTION_V2, 315);
    fixture.executor.run_all();
    check(fixture.quic.inspections == 1 &&
              fixture.quic.last_resource == 315,
          "public engine inspects the actual QUIC handshake");
    flynes::session::wire::QuicHandshakeFactsV2 handshake{};
    handshake.tls_major = 1;
    handshake.tls_minor = 3;
    handshake.full_handshake = true;
    std::copy_n("flynes-nearby/2", 16, handshake.alpn.begin());
    std::array<std::uint8_t,
               flynes::session::wire::kQuicHandshakeFactsWireSizeV2>
        handshake_bytes{};
    check(flynes::session::wire::encode_quic_handshake_facts_v2(
              handshake, &handshake_bytes) ==
              flynes::session::wire::Status::Ok,
          "test encodes canonical listener handshake facts");
    const auto handshake_hash = flynes::session::wire::sha256(
        handshake_bytes.data(), handshake_bytes.size());
    deliver_provider_hash_buffer(
        fixture.quic.inbox, fixture.quic.last_token,
        FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2, 315,
        handshake_bytes.data(), handshake_bytes.size(), handshake_hash);
    fixture.executor.run_all();
    check(fixture.quic.exporters == 1 &&
              fixture.quic.last_label == std::vector<std::uint8_t>(
                  {'E','X','P','O','R','T','E','R','-','f','l','y','n','e','s','-',
                   'n','e','a','r','b','y','-','v','1'}) &&
              fixture.quic.last_context.size() == 32,
          "verified full handshake requests the exact TLS exporter");
    std::array<std::uint8_t, 32> exporter{};
    exporter[0] = 0x41;
    deliver_provider_buffer(
        fixture.quic.inbox, fixture.quic.last_token,
        FLY_SESSION_PROVIDER_QUIC_EXPORTER_V2,
        exporter.data(), exporter.size());
    fixture.executor.run_all();
    check(fixture.quic.accepted_bidi == 1 &&
              fixture.quic.opened_bidi == 0,
          "listener accepts exactly one connector-opened bind stream");
    deliver_provider_resource_pair(
        fixture.quic.inbox, fixture.quic.last_token,
        FLY_SESSION_PROVIDER_QUIC_STREAM_V2, 316, 317);
    fixture.executor.run_all();

    std::array<std::uint8_t, 16> session_id{};
    std::copy_n(context.bytes.begin() + 32, session_id.size(),
                session_id.begin());
    const auto channel_id = flynes::session::wire::derive_channel_id_v1(
        transcript_hash, session_id, {}, exporter);
    std::array<std::uint8_t,
               flynes::session::wire::kChannelBindSizeV1> bind{};
    std::array<std::uint8_t,
               flynes::session::wire::kChannelBindProofBodySizeV1>
        connector_body{};
    std::array<std::uint8_t, 32> connector_tag{};
    connector_tag[0] = 0x51;
    std::array<std::uint8_t,
               flynes::session::wire::kChannelBindProofSizeV1>
        connector_proof{};
    check(flynes::session::wire::encode_initial_channel_bind_v1(
              transcript_hash, session_id, channel_id, &bind) ==
              flynes::session::wire::Status::Ok &&
              flynes::session::wire::encode_channel_bind_proof_body_v1(
                  bind, flynes::session::wire::PairRoleV1::Responder,
                  point, point, &connector_body) ==
              flynes::session::wire::Status::Ok &&
              flynes::session::wire::encode_channel_bind_proof_v1(
                  connector_body, connector_tag, &connector_proof) ==
              flynes::session::wire::Status::Ok,
          "test constructs the authenticated connector ChannelBind proof");
    const auto preamble = flynes::session::wire::bind_stream_preamble_v1();
    std::vector<std::uint8_t> connector_record(
        preamble.begin(), preamble.end());
    connector_record.push_back(0);
    connector_record.push_back(0);
    connector_record.push_back(0);
    connector_record.push_back(static_cast<std::uint8_t>(
        flynes::session::wire::kChannelBindProofSizeV1));
    connector_record.insert(connector_record.end(), connector_proof.begin(),
                            connector_proof.end());
    deliver_provider_stream_data(
        fixture.quic.inbox, fixture.quic.last_token,
        connector_record.data(), connector_record.size());
    fixture.executor.run_all();
    check(fixture.crypto.last_resource == 314,
          "listener verifies responder connector proof with the r2i bind key");
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        connector_tag.data(), connector_tag.size());
    fixture.executor.run_all();
    check(fixture.crypto.last_resource == 313,
          "verified connector proof authorizes the initiator listener proof HMAC");
    std::array<std::uint8_t, 32> listener_tag{};
    listener_tag[0] = 0x52;
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        listener_tag.data(), listener_tag.size());
    fixture.executor.run_all();
    check(fixture.quic.writes == 1 && fixture.quic.last_finish == 0 &&
              fixture.quic.last_write.size() ==
                  4 + flynes::session::wire::kChannelBindProofSizeV1,
          "listener publishes one framed proof without early FIN");
    std::array<std::uint8_t,
               flynes::session::wire::kChannelBindProofSizeV1>
        listener_proof{};
    if (fixture.quic.last_write.size() == 4 + listener_proof.size())
        std::copy_n(fixture.quic.last_write.begin() + 4,
                    listener_proof.size(), listener_proof.begin());
    deliver_provider_end(
        fixture.quic.inbox, fixture.quic.last_token,
        FLY_SESSION_PROVIDER_QUIC_END_V2);
    fixture.executor.run_all();

    const auto connector_proof_hash =
        flynes::session::wire::channel_bind_proof_hash_v1(connector_proof);
    const auto listener_proof_hash =
        flynes::session::wire::channel_bind_proof_hash_v1(listener_proof);
    std::array<std::uint8_t,
               flynes::session::wire::kChannelBindAckBodySizeV1> ack_body{};
    std::array<std::uint8_t, 32> channel_ack_tag{};
    channel_ack_tag[0] = 0x53;
    std::array<std::uint8_t,
               flynes::session::wire::kChannelBindAckSizeV1> ack{};
    check(flynes::session::wire::encode_channel_bind_ack_body_v1(
              channel_id, connector_proof_hash, listener_proof_hash,
              flynes::session::wire::PairRoleV1::Responder, &ack_body) ==
              flynes::session::wire::Status::Ok &&
              flynes::session::wire::encode_channel_bind_ack_v1(
                  ack_body, channel_ack_tag, &ack) ==
              flynes::session::wire::Status::Ok,
          "test constructs the connector ACK over both exact proofs");
    std::vector<std::uint8_t> ack_record{0, 0, 0,
        static_cast<std::uint8_t>(
            flynes::session::wire::kChannelBindAckSizeV1)};
    ack_record.insert(ack_record.end(), ack.begin(), ack.end());
    deliver_provider_stream_data(
        fixture.quic.inbox, fixture.quic.last_token,
        ack_record.data(), ack_record.size());
    fixture.executor.run_all();
    deliver_provider_buffer(
        fixture.crypto.inbox, fixture.crypto.last_token,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        channel_ack_tag.data(), channel_ack_tag.size());
    fixture.executor.run_all();
    deliver_provider_stream_data(
        fixture.quic.inbox, fixture.quic.last_token, nullptr, 0);
    fixture.executor.run_all();
    check(fixture.quic.writes == 2 && fixture.quic.last_finish == 1 &&
              fixture.quic.last_write.empty(),
          "listener observes connector FIN before returning its own FIN");
    deliver_provider_end(
        fixture.quic.inbox, fixture.quic.last_token,
        FLY_SESSION_PROVIDER_QUIC_END_V2);
    fixture.executor.run_all();

    drive_session_signing_persistence(fixture, tail, transcript_hash,
                                      session_id, point);
    fixture.bearer.cancel_result = FLY_SESSION_V2_OK;
    for (auto& action : actions)
        fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : sas_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : after_confirm_actions)
        fly_session_approval_token_release_v2(action.approval_token);
}

void test_public_candidate_projection_and_stop()
{
    EngineFixture fixture;
    fixture.platform.ready();
    fixture.executor.run_all();
    std::vector<fly_session_action_descriptor_v2> idle_actions;
    fixture.snapshot(&idle_actions);
    const auto* start = find_action(
        idle_actions, FLY_SESSION_ACTION_START_DISCOVERY_V2);
    check(start != nullptr, "idle view exposes bounded discovery start");
    if (!start) return;
    submit(fixture, *start, 401, false);
    check(fixture.snapshot().link_state == FLY_SESSION_LINK_DISCOVERING_V2,
          "start discovery publishes the shared discovering state");

    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = fixture.discovery.last_token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = FLY_SESSION_PROVIDER_DISCOVERY_CANDIDATE_V2;
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = 71;
    payload.generation = event.token.connection_generation;
    payload.value0 = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(-2));
    payload.value1 = 123456;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    check(fly_session_deliver_v2(fixture.discovery.inbox, &event) ==
              FLY_SESSION_V2_ACCEPTED,
          "typed anonymous candidate is accepted by the public inbox");
    fixture.executor.run_all();

    fly_session_view_v2_t* view = nullptr;
    check(fly_session_acquire_view_v2(fixture.engine, &view) ==
              FLY_SESSION_V2_OK,
          "candidate view acquired");
    fly_session_snapshot_v2 snapshot{};
    snapshot.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE;
    snapshot.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_view_read_v2(view, &snapshot) == FLY_SESSION_V2_OK &&
              snapshot.candidate_count == 1,
          "candidate count belongs to one immutable view revision");
    fly_session_candidate_v2 candidate{};
    std::uint32_t written = 0;
    check(fly_session_view_copy_candidates_v2(
              view, 0, &candidate, 1, &written) == FLY_SESSION_V2_OK &&
              written == 1 && candidate.candidate == 71 &&
              candidate.discovery_generation == payload.generation &&
              candidate.rssi_bucket == -2 &&
              candidate.last_observed_continuous_ns == payload.value1,
          "candidate page exposes only temporary local discovery facts");
    fly_session_view_release_v2(view);

    std::vector<fly_session_action_descriptor_v2> discovering_actions;
    fixture.snapshot(&discovering_actions);
    const auto* join_candidate = find_action(
        discovering_actions, FLY_SESSION_ACTION_JOIN_CANDIDATE_V2);
    check(join_candidate != nullptr,
          "discovering view exposes candidate join authorization");
    if (join_candidate)
    {
        submit_reference(fixture, *join_candidate, 402, 71);
        check(fixture.discovery.connects == 1 &&
                  fixture.discovery.last_candidate == 71 &&
                  fixture.discovery.last_candidate_generation ==
                      payload.generation &&
                  fixture.snapshot().link_state == FLY_SESSION_LINK_JOINING_V2,
              "candidate join connects the exact current ephemeral handle");
    }
    const auto* stop = find_action(
        discovering_actions, FLY_SESSION_ACTION_STOP_DISCOVERY_V2);
    check(stop != nullptr, "discovering view exposes exact stop action");
    if (stop && !join_candidate)
    {
        submit(fixture, *stop, 403, false);
        const auto stopped = fixture.snapshot();
        check(stopped.link_state == FLY_SESSION_LINK_IDLE_V2 &&
                  stopped.candidate_count == 0,
              "stop discovery clears ephemeral candidates and returns idle");
    }
    for (auto& action : idle_actions)
        fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : discovering_actions)
        fly_session_approval_token_release_v2(action.approval_token);
}

void test_inviter_generates_and_sends_pair_context_from_crypto_port()
{
    EngineFixture fixture;
    drive_initiator_pair_flow(fixture,
                              SessionSigningTail::DurableAcrossBothGates);
}

void test_session_signing_binding_object_hash_mismatch_fails_closed()
{
    EngineFixture fixture;
    drive_initiator_pair_flow(fixture, SessionSigningTail::WrongObjectHash);
}

void test_session_signing_binding_shutdown_cancels_through_object_store()
{
    EngineFixture fixture;
    drive_initiator_pair_flow(
        fixture, SessionSigningTail::ShutdownCancelsThroughObjectStore);
}

/*
 * Decision 1 negatives for the R3 object-store read primitive, driven through
 * the real public engine and the real link handshake mount point: an absent
 * object and an object whose bytes disagree with the expected content hash are
 * two different failures, and neither may be mistaken for the other or for a
 * successful re-read.
 */
void test_link_handshake_read_object_missing_fails_closed()
{
    EngineFixture fixture;
    drive_initiator_pair_flow(fixture, SessionSigningTail::ReadObjectMissing);
}

void test_link_handshake_read_object_hash_mismatch_fails_closed()
{
    EngineFixture fixture;
    drive_initiator_pair_flow(fixture,
                              SessionSigningTail::ReadObjectContentMismatch);
}

} // namespace

int main()
{
    test_public_ready_and_invitation_projection();
    test_connection_without_secure_pairing_ports_fails_closed();
    test_shutdown_waits_for_async_discovery_terminal();
    test_shutdown_disconnects_late_discovery_connection();
    test_shutdown_waits_for_active_bearer_probe();
    test_shutdown_waits_for_active_pair_material_and_releases_racing_key();
    test_inviter_generates_and_sends_pair_context_from_crypto_port();
    test_session_signing_binding_object_hash_mismatch_fails_closed();
    test_session_signing_binding_shutdown_cancels_through_object_store();
    test_link_handshake_read_object_missing_fails_closed();
    test_link_handshake_read_object_hash_mismatch_fails_closed();
    test_public_candidate_projection_and_stop();
    return failures == 0 ? 0 : 1;
}
