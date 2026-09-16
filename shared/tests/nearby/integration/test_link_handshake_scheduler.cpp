#include "link/link_handshake_scheduler.hpp"
#include "buffer_handle.hpp"
#include "wire/app_frame.hpp"
#include "wire/p256_point.hpp"
#include "wire/session_signing_binding.hpp"
#include "wire/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

/*
 * W1 Task 4 integration: the LINK_HELLO / LINK_READY handshake scheduler driven
 * end to end through effects, with a provider stub that plays the engine's role.
 * Both sides use the same exact codecs from Task 3.
 */

namespace {

namespace wire = flynes::session::wire;
namespace link = flynes::session::link;

using flynes::session::LinkHandshakeEffect;
using flynes::session::LinkHandshakeEffectKind;
using flynes::session::LinkHandshakeScheduler;
using flynes::session::LinkHandshakeStageV1;
using flynes::session::LinkHandshakeStartV1;

int failures = 0;

void check(bool value, const char* message)
{
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::uint8_t nibble(char value)
{
    return static_cast<std::uint8_t>(
        value <= '9' ? value - '0' : value - 'a' + 10);
}

template <std::size_t N>
std::array<std::uint8_t, N> hex_bytes(const char* hex)
{
    std::array<std::uint8_t, N> out{};
    for (std::size_t index = 0; index < N; ++index)
        out[index] = static_cast<std::uint8_t>(
            (nibble(hex[index * 2]) << 4u) | nibble(hex[index * 2 + 1]));
    return out;
}

constexpr const char* kGenerator1 =
    "046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
    "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5";
constexpr const char* kGenerator2 =
    "047cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978"
    "07775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1";

std::array<std::uint8_t, 64> test_sign(
    const std::array<std::uint8_t, 65>& public_key,
    const std::array<std::uint8_t, 32>& digest)
{
    std::array<std::uint8_t, 64> out{};
    for (std::size_t part = 0; part < 2; ++part) {
        std::array<std::uint8_t, 1 + 65 + 32> buffer{};
        buffer[0] = static_cast<std::uint8_t>(part == 0 ? 1 : 2);
        std::copy(public_key.begin(), public_key.end(), buffer.begin() + 1);
        std::copy(digest.begin(), digest.end(), buffer.begin() + 66);
        const auto hash = wire::sha256(buffer.data(), buffer.size());
        std::copy(hash.begin(), hash.end(),
                  out.begin() + static_cast<std::ptrdiff_t>(part * 32));
    }
    out[0] = static_cast<std::uint8_t>((out[0] & 0x7f) | 0x01);
    out[32] = static_cast<std::uint8_t>(0x40 | (out[32] & 0x1f));
    return out;
}

bool test_verify(void*, const std::uint8_t public_key[65],
                 const std::uint8_t digest[32],
                 const std::uint8_t signature[64])
{
    std::array<std::uint8_t, 65> key{};
    std::copy_n(public_key, 65, key.begin());
    std::array<std::uint8_t, 32> message{};
    std::copy_n(digest, 32, message.begin());
    const auto expected = test_sign(key, message);
    return std::memcmp(expected.data(), signature, 64) == 0;
}

/* ------------------------------------------------ shared link fixture ---- */

const std::array<std::uint8_t, 16>& session_id()
{
    static const std::array<std::uint8_t, 16> value = [] {
        std::array<std::uint8_t, 16> out{};
        out.fill(0x11);
        return out;
    }();
    return value;
}
const std::array<std::uint8_t, 16>& link_id()
{
    static const std::array<std::uint8_t, 16> value = [] {
        std::array<std::uint8_t, 16> out{};
        out.fill(0x22);
        return out;
    }();
    return value;
}
template <std::size_t N>
std::array<std::uint8_t, N> filled(std::uint8_t value)
{
    std::array<std::uint8_t, N> out{};
    out.fill(value);
    return out;
}

const std::array<std::uint8_t, 32>& transcript_hash()
{
    static const auto value = filled<32>(0x55);
    return value;
}
const std::array<std::uint8_t, 32>& transcript_object_hash()
{
    static const auto value = filled<32>(0x56);
    return value;
}
const std::array<std::uint8_t, 32>& plan_hash()
{
    static const auto value = filled<32>(0x33);
    return value;
}
const std::array<std::uint8_t, 32>& offer_hash()
{
    static const auto value = filled<32>(0x44);
    return value;
}
const std::array<std::uint8_t, 32>& channel_bind_hash()
{
    static const auto value = filled<32>(0x88);
    return value;
}
const std::array<std::uint8_t, 32>& merge_hash()
{
    static const auto value = filled<32>(0xdd);
    return value;
}
const std::array<std::uint8_t, 32>& summary_a()
{
    static const auto value = filled<32>(0xbb);
    return value;
}
const std::array<std::uint8_t, 32>& summary_b()
{
    static const auto value = filled<32>(0xcc);
    return value;
}

constexpr std::array<std::uint8_t, 16> kChannelId = {{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
constexpr std::uint64_t kGeneration = 7;
constexpr std::uint64_t kLinkGeneration = 9;

/* ------------------------------------------------------------ provider ---- */

struct Side
{
    LinkHandshakeScheduler scheduler;
    std::array<std::uint8_t, wire::kSessionSigningBindingSizeV1> binding{};
    std::array<std::uint8_t, 32> binding_hash{};
    std::array<std::uint8_t, 65> identity_public{};
    std::array<std::uint8_t, 65> session_public{};
    std::array<std::uint8_t, 32> identity_key_id{};
    /* There is deliberately no negotiated-result fixture: the scheduler derives
     * it from the two HELLO object hashes, so a test-side copy would be a second
     * definition of the value under test. */
    std::array<std::uint8_t, 32> local_summary{};
    std::array<std::uint8_t, 32> peer_summary{};
    /* Stub policy. */
    std::optional<LinkHandshakeEffectKind> fail_kind{};
    bool fail_always = false;
    /* Evidence. */
    std::vector<LinkHandshakeEffectKind> answered{};
    std::vector<std::uint64_t> operation_ids{};
    int send_count = 0;
    fly_session_result_v2 last_result = FLY_SESSION_V2_OK;
    /* gap 4 loopback. outbox holds the exact framed records this side wrote on
     * its Control stream; inbox holds peer bytes not yet delivered to a read. */
    std::vector<std::vector<std::uint8_t>> outbox{};
    std::vector<std::vector<std::uint8_t>> inbox{};
    fly_session_resource_handle_v2 next_stream_handle = 0x4000;
};

fly_session_port_event_v2 end_event(const fly_session_op_token_v2& token,
                                    std::uint32_t kind,
                                    fly_session_result_v2 result)
{
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = result;
    event.payload_kind = kind;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 resource_event(const fly_session_op_token_v2& token,
                                         std::uint32_t kind,
                                         std::uint64_t resource)
{
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    payload.generation = token.connection_generation;
    auto event = end_event(token, kind, FLY_SESSION_V2_OK);
    event.payload_kind = kind;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

/*
 * The two-handle terminal an open_bidi/accept_bidi answers with: the send stream
 * in `resource`, the receive stream in `value0` (same shape the bind stream used).
 */
fly_session_port_event_v2 stream_event(const fly_session_op_token_v2& token,
                                       std::uint32_t kind,
                                       fly_session_resource_handle_v2 send_stream,
                                       fly_session_resource_handle_v2 receive_stream)
{
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = send_stream;
    payload.generation = token.connection_generation;
    payload.value0 = receive_stream;
    auto event = end_event(token, kind, FLY_SESSION_V2_OK);
    event.payload_kind = kind;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

/*
 * A QUIC data delivery is NON-terminal by port contract (terminal = 0): one
 * granted read credit may be answered with several data events. Building it as a
 * terminal event would be a contract violation before it ever reached the
 * scheduler, so this constructor is deliberately separate from end_event().
 */
fly_session_port_event_v2 data_event(const fly_session_op_token_v2& token,
                                     const std::uint8_t* bytes,
                                     std::size_t size)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{bytes,
                                      static_cast<std::uint32_t>(size), 0};
    check(fly_session_buffer_create_copy_v2(source, &buffer) ==
              FLY_SESSION_V2_OK,
          "fixture creates a provider buffer");
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = buffer;
    payload.logical_size = size;
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = token.operation_id;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 0;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = FLY_SESSION_PROVIDER_QUIC_DATA_V2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 buffer_event(const fly_session_op_token_v2& token,
                                       std::uint32_t kind,
                                       const std::uint8_t* bytes,
                                       std::size_t size)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{bytes,
                                      static_cast<std::uint32_t>(size), 0};
    check(fly_session_buffer_create_copy_v2(source, &buffer) ==
              FLY_SESSION_V2_OK,
          "fixture creates a provider buffer");
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = buffer;
    payload.logical_size = size;
    auto event = end_event(token, kind, FLY_SESSION_V2_OK);
    event.payload_kind = kind;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 hash_event(const fly_session_op_token_v2& token,
                                     std::uint32_t kind,
                                     const std::uint8_t* bytes,
                                     std::size_t size,
                                     const std::array<std::uint8_t, 32>& hash)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{bytes,
                                      static_cast<std::uint32_t>(size), 0};
    check(fly_session_buffer_create_copy_v2(source, &buffer) ==
              FLY_SESSION_V2_OK,
          "fixture creates a provider buffer");
    fly_session_provider_hash_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_HASH_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = 1;
    payload.buffer = buffer;
    std::copy(hash.begin(), hash.end(), std::begin(payload.hash));
    auto event = end_event(token, kind, FLY_SESSION_V2_OK);
    event.payload_kind = kind;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_result_v2 complete_event(LinkHandshakeScheduler& scheduler,
                                     const fly_session_port_event_v2& event)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    if (event.result == FLY_SESSION_V2_OK) {
        if (event.payload_kind == FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2) {
            fly_session_provider_hash_event_v2 payload{};
            std::memcpy(&payload, event.payload, sizeof(payload));
            buffer = payload.buffer;
        } else if (event.payload_kind ==
                   FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2) {
            fly_session_provider_buffer_event_v2 payload{};
            std::memcpy(&payload, event.payload, sizeof(payload));
            buffer = payload.buffer;
        } else if (event.payload_kind == FLY_SESSION_PROVIDER_QUIC_DATA_V2) {
            fly_session_provider_buffer_event_v2 payload{};
            std::memcpy(&payload, event.payload, sizeof(payload));
            buffer = payload.buffer;
        }
    }
    const auto result = scheduler.complete(event);
    if (buffer != nullptr) fly_session_buffer_release_v2(buffer);
    return result;
}

Side make_side(wire::PairRoleV1 role,
               const std::array<std::uint8_t, 65>& identity,
               const std::array<std::uint8_t, 65>& session,
               const std::array<std::uint8_t, 65>& peer_identity,
               const std::array<std::uint8_t, 65>& peer_session,
               const std::array<std::uint8_t, 32>& peer_binding_hash,
               const std::array<std::uint8_t, 32>& local_summary,
               const std::array<std::uint8_t, 32>& peer_summary,
               std::uint64_t first_operation_id, std::uint64_t generation)
{
    Side side;
    side.identity_public = identity;
    side.session_public = session;
    side.identity_key_id = wire::link_identity_key_id_v1(identity.data());
    /* Distinct per-role handle values, so the loopback test can prove each side
     * uses the handle its own open/accept answered with. */
    side.next_stream_handle = role == wire::PairRoleV1::Initiator ? 0x4000 : 0x4100;
    side.local_summary = local_summary;
    side.peer_summary = peer_summary;

    std::array<std::uint8_t, wire::kSessionSigningBindingPretagSizeV1> pretag{};
    std::array<std::uint8_t, 32> digest{};
    check(wire::build_session_signing_binding_pretag_v1(
              transcript_hash(), session_id(), role, identity, session,
              wire::validate_p256_uncompressed_point_callback, nullptr, &pretag,
              &digest) == wire::Status::Ok,
          "fixture builds the local 0x0212 binding pretag");
    const auto signature = test_sign(session, digest);
    check(wire::finish_session_signing_binding_v1(pretag, signature,
                                                  &side.binding,
                                                  &side.binding_hash) ==
              wire::Status::Ok,
          "fixture persists a canonical local 0x0212 binding");

    LinkHandshakeStartV1 start{};
    start.engine_instance_id.fill(0x31);
    start.link_id = link_id();
    start.session_id = session_id();
    start.generation = generation;
    start.link_generation = kLinkGeneration;
    start.channel_id = kChannelId;
    start.reconnect_attempt = 0;
    start.first_operation_id = first_operation_id;
    start.local_role = role;
    start.determinism_profile = 1;
    start.core_state_format = 2;
    start.pair_transcript_hash = transcript_hash();
    start.pair_transcript_object_hash = transcript_object_hash();
    start.selected_plan_hash = plan_hash();
    start.endpoint_offer_hash = offer_hash();
    start.channel_bind_hash = channel_bind_hash();
    start.local_binding_hash = side.binding_hash;
    start.local_identity_public_key = identity;
    start.local_session_signing_public_key = session;
    start.peer_binding_hash = peer_binding_hash;
    start.peer_identity_key_id = wire::link_identity_key_id_v1(
        peer_identity.data());
    start.peer_identity_public_key = peer_identity;
    start.peer_session_signing_public_key = peer_session;
    start.local_summary_hash = local_summary;
    start.peer_summary_hash = peer_summary;
    start.merge_result_hash = merge_hash();
    start.session_signing_key = 0x1000;
    start.quic_connection = 0x3000;
    start.local_is_listener = role == wire::PairRoleV1::Responder;
    check(side.scheduler.begin(start), "scheduler begins a link attempt");
    return side;
}

/* The asynchronous verification terminal the engine delivers after the crypto
 * port has checked the peer's signature over effect.digest. */
fly_session_port_event_v2 verification_event(
    const fly_session_op_token_v2& token, fly_session_result_v2 result)
{
    return end_event(token, FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2, result);
}

fly_session_result_v2 answer(Side& side, const LinkHandshakeEffect& effect)
{
    const auto& token = effect.token;
    side.answered.push_back(effect.kind);
    side.operation_ids.push_back(token.operation_id);
    const bool inject_failure =
        side.fail_always ||
        (side.fail_kind.has_value() && *side.fail_kind == effect.kind);
    switch (effect.kind) {
    case LinkHandshakeEffectKind::OpenControlStream: {
        /*
         * gap 4. The Control stream open/accept terminal: the send handle in
         * `resource`, the receive handle in `value0`, exactly the shape the bind
         * stream used. Distinct handles per side so a test cannot confuse them.
         */
        if (inject_failure)
            return complete_event(side.scheduler,
                                  end_event(token, effect.expected_payload_kind,
                                            FLY_SESSION_V2_IO_FAILED));
        const auto send = side.next_stream_handle++;
        const auto receive = side.next_stream_handle++;
        return complete_event(
            side.scheduler,
            stream_event(token, FLY_SESSION_PROVIDER_QUIC_STREAM_V2, send,
                         receive));
    }
    case LinkHandshakeEffectKind::ReadControlBytes: {
        /*
         * gap 4. Deliver whatever the loopback has buffered for this side. A read
         * with nothing to deliver must never reach here: pump() blocks instead,
         * so a test cannot silently complete a read with no bytes.
         */
        if (inject_failure)
            return complete_event(side.scheduler,
                                  end_event(token, effect.expected_payload_kind,
                                            FLY_SESSION_V2_IO_FAILED));
        if (side.inbox.empty())
            return FLY_SESSION_V2_INVALID_STATE;
        auto bytes = std::move(side.inbox.front());
        side.inbox.erase(side.inbox.begin());
        return complete_event(
            side.scheduler,
            data_event(token, bytes.data(), bytes.size()));
    }
    case LinkHandshakeEffectKind::ReadLocalBindingObject:
        if (inject_failure)
            return complete_event(side.scheduler,
                                  end_event(token, effect.expected_payload_kind, FLY_SESSION_V2_IO_FAILED));
        return complete_event(
            side.scheduler,
            hash_event(token, FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2,
                       side.binding.data(), side.binding.size(),
                       side.binding_hash));
    case LinkHandshakeEffectKind::SignHello:
    case LinkHandshakeEffectKind::SignReady:
    case LinkHandshakeEffectKind::SignAck: {
        if (inject_failure)
            return complete_event(side.scheduler,
                                  end_event(token, effect.expected_payload_kind, FLY_SESSION_V2_IO_FAILED));
        const auto signature = test_sign(side.session_public, effect.digest);
        return complete_event(
            side.scheduler,
            buffer_event(token, FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2,
                         signature.data(), signature.size()));
    }
    case LinkHandshakeEffectKind::VerifyPeerSignature: {
        /*
         * The engine's asynchronous crypto port terminal. This stub runs the
         * same real check the provider must run - the canonical test verifier
         * over the exact digest the scheduler computed - and reports the outcome
         * through the inbox, so a tampered signature is still rejected and only
         * the rejection point moved from a synchronous callback to this result.
         */
        if (inject_failure)
            return complete_event(
                side.scheduler,
                verification_event(token, FLY_SESSION_V2_IO_FAILED));
        const bool verified =
            test_verify(nullptr, effect.signer_public_key.data(),
                        effect.digest.data(), effect.signature.data());
        return complete_event(
            side.scheduler,
            verification_event(token, verified ? FLY_SESSION_V2_OK
                                               : FLY_SESSION_V2_AUTH_FAILED));
    }
    case LinkHandshakeEffectKind::PersistHelloObject:
    case LinkHandshakeEffectKind::PersistPeerHelloObject:
    case LinkHandshakeEffectKind::PersistReadyObject:
    case LinkHandshakeEffectKind::PersistPeerReadyObject:
    case LinkHandshakeEffectKind::PersistAckObject:
    case LinkHandshakeEffectKind::PersistPeerAckObject:
        if (inject_failure)
            return complete_event(side.scheduler,
                                  end_event(token, effect.expected_payload_kind, FLY_SESSION_V2_IO_FAILED));
        return complete_event(
            side.scheduler,
            hash_event(token, FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2,
                       effect.value.data(), effect.value.size(),
                       effect.expected_hash));
    case LinkHandshakeEffectKind::PersistNegotiatedResult:
        if (inject_failure)
            return complete_event(side.scheduler,
                                  end_event(token, effect.expected_payload_kind, FLY_SESSION_V2_IO_FAILED));
        return complete_event(
            side.scheduler,
            resource_event(token, FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2,
                           1));
    case LinkHandshakeEffectKind::SendHello:
    case LinkHandshakeEffectKind::SendReady:
    case LinkHandshakeEffectKind::SendAck:
    case LinkHandshakeEffectKind::SendLocalBinding:
        ++side.send_count;
        if (inject_failure)
            return complete_event(side.scheduler,
                                  end_event(token, effect.expected_payload_kind, FLY_SESSION_V2_IO_FAILED));
        /* gap 4: the write really happened, so the exact framed record goes into
         * the loopback outbox and will be delivered to the peer's next read. */
        side.outbox.push_back(effect.value);
        return complete_event(side.scheduler,
                              end_event(token, effect.expected_payload_kind, FLY_SESSION_V2_OK));
    }
    return FLY_SESSION_V2_INVALID_STATE;
}

/* Answers effects until the scheduler has nothing pending, or until it is
 * waiting for Control bytes the loopback has not delivered yet. */
int pump(Side& side, bool* blocked = nullptr)
{
    if (blocked != nullptr) *blocked = false;
    int answered = 0;
    while (true) {
        const auto effect = side.scheduler.poll_effect();
        if (!effect) break;
        /*
         * gap 4. A Control read cannot be completed by this side alone: it needs
         * bytes the PEER wrote. Leave it pending and report blocked, so the
         * loopback driver can give the other side a turn.
         */
        if (effect->kind == LinkHandshakeEffectKind::ReadControlBytes &&
            side.inbox.empty()) {
            if (blocked != nullptr) *blocked = true;
            break;
        }
        const auto result = answer(side, *effect);
        side.last_result = result;
        ++answered;
        if (result != FLY_SESSION_V2_OK) break;
    }
    return answered;
}

/* Counts how many of the recorded effects match a given kind. */
int count_kind(const Side& side, LinkHandshakeEffectKind kind)
{
    return static_cast<int>(std::count(side.answered.begin(),
                                       side.answered.end(), kind));
}

int index_of(const Side& side, LinkHandshakeEffectKind kind)
{
    const auto found = std::find(side.answered.begin(), side.answered.end(),
                                 kind);
    return found == side.answered.end()
               ? -1
               : static_cast<int>(found - side.answered.begin());
}

/* ------------------------------------------------------------- fixtures -- */

/* Derives a side's 0x0212 binding hash the same way make_side does, so the two
 * sides can reference each other's binding before either scheduler starts. */
std::array<std::uint8_t, 32> binding_hash_for(
    wire::PairRoleV1 role, const std::array<std::uint8_t, 65>& identity,
    const std::array<std::uint8_t, 65>& session)
{
    std::array<std::uint8_t, wire::kSessionSigningBindingPretagSizeV1> pretag{};
    std::array<std::uint8_t, 32> digest{};
    if (wire::build_session_signing_binding_pretag_v1(
            transcript_hash(), session_id(), role, identity, session,
            wire::validate_p256_uncompressed_point_callback, nullptr, &pretag,
            &digest) != wire::Status::Ok)
        return {};
    std::array<std::uint8_t, wire::kSessionSigningBindingSizeV1> binding{};
    std::array<std::uint8_t, 32> hash{};
    if (wire::finish_session_signing_binding_v1(pretag, test_sign(session, digest),
                                                &binding, &hash) !=
        wire::Status::Ok)
        return {};
    return hash;
}

struct Pair
{
    Side a;
    Side b;
};

Pair make_pair(std::uint64_t generation, std::uint64_t first_operation_id)
{
    const auto g1 = hex_bytes<65>(kGenerator1);
    const auto g2 = hex_bytes<65>(kGenerator2);
    const auto a_binding_hash = binding_hash_for(
        wire::PairRoleV1::Initiator, /* identity */ g2, /* session */ g1);
    const auto b_binding_hash = binding_hash_for(
        wire::PairRoleV1::Responder, /* identity */ g1, /* session */ g2);
    Pair pair{
        make_side(wire::PairRoleV1::Initiator, /* identity */ g2,
                  /* session */ g1, /* peer identity */ g1,
                  /* peer session */ g2, b_binding_hash, summary_a(),
                  summary_b(), first_operation_id, generation),
        make_side(wire::PairRoleV1::Responder, /* identity */ g1,
                  /* session */ g2, /* peer identity */ g2,
                  /* peer session */ g1, a_binding_hash, summary_b(),
                  summary_a(), first_operation_id + 2, generation)};
    return pair;
}

/*
 * gap 4 loopback: a strict in-order, in-memory Control stream between the two
 * sides. Every framed record one side writes on its Control stream is handed,
 * byte for byte, to the other side's next Control read — the same thing a QUIC
 * bidirectional stream does. Nothing is fabricated: the bytes are exactly what
 * the sender's encoder produced, and the receiver deframes them through
 * app_frame before the handshake sees them.
 */
struct LoopbackResult
{
    int rounds = 0;
    bool stalled = false;
};

LoopbackResult run_control_loopback(Pair& pair, int maximum_rounds)
{
    LoopbackResult out{};
    for (int round = 0; round < maximum_rounds; ++round) {
        out.rounds = round + 1;
        bool a_blocked = false;
        bool b_blocked = false;
        pump(pair.a, &a_blocked);
        pump(pair.b, &b_blocked);
        if (pair.a.scheduler.failed() || pair.b.scheduler.failed()) return out;

        for (auto& frame : pair.a.outbox) pair.b.inbox.push_back(frame);
        pair.a.outbox.clear();
        for (auto& frame : pair.b.outbox) pair.a.inbox.push_back(frame);
        pair.b.outbox.clear();

        /* Neither side is waiting for bytes and neither has any left to deliver,
         * so the exchange is finished. */
        if (!a_blocked && !b_blocked) return out;
    }
    out.stalled = true;
    return out;
}

/* Runs both sides up to AwaitPeerHello. */
void exchange_hellos(Pair& pair)
{
    pump(pair.a);
    pump(pair.b);
}

/*
 * The frozen legal stage order, now including gap 4. The Control stream is opened
 * first, and each "awaiting the peer message" milestone is followed by the
 * Control read that actually fetches it — which is exactly the wiring that was
 * missing when the handshake could never receive a peer byte.
 */
const std::array<LinkHandshakeStageV1, 27>& expected_sequence()
{
    static const std::array<LinkHandshakeStageV1, 27> value{{
        LinkHandshakeStageV1::OpenControl,
        LinkHandshakeStageV1::ReadLocalBinding,
        LinkHandshakeStageV1::SendLocalBinding,
        LinkHandshakeStageV1::SignHello,
        LinkHandshakeStageV1::PersistHello,
        LinkHandshakeStageV1::SendHello,
        LinkHandshakeStageV1::AwaitPeerHello,
        LinkHandshakeStageV1::ReadPeerControlBytes,
        LinkHandshakeStageV1::ReadPeerControlBytes,
        LinkHandshakeStageV1::AwaitPeerHelloSignature,
        LinkHandshakeStageV1::PersistPeerHello,
        LinkHandshakeStageV1::PersistNegotiatedResult,
        LinkHandshakeStageV1::SignReady,
        LinkHandshakeStageV1::PersistReady,
        LinkHandshakeStageV1::SendReady,
        LinkHandshakeStageV1::AwaitPeerReady,
        LinkHandshakeStageV1::ReadPeerControlBytes,
        LinkHandshakeStageV1::AwaitPeerReadySignature,
        LinkHandshakeStageV1::PersistPeerReady,
        LinkHandshakeStageV1::SignAck,
        LinkHandshakeStageV1::PersistAck,
        LinkHandshakeStageV1::SendAck,
        LinkHandshakeStageV1::AwaitPeerAck,
        LinkHandshakeStageV1::ReadPeerControlBytes,
        LinkHandshakeStageV1::AwaitPeerAckSignature,
        LinkHandshakeStageV1::PersistPeerAck,
        LinkHandshakeStageV1::Connected}};
    return value;
}

/*
 * The same legal order when the peer message is injected directly through
 * accept_peer_* instead of arriving as Control bytes. One stage differs and it is
 * not a shortcut: the accepting read is displaced by the direct call, so the
 * extra ReadPeerControlBytes that the loopback needs for the peer's 0x0212
 * object never happens.
 */
const std::array<LinkHandshakeStageV1, 26>& manual_sequence()
{
    static const std::array<LinkHandshakeStageV1, 26> value{{
        LinkHandshakeStageV1::OpenControl,
        LinkHandshakeStageV1::ReadLocalBinding,
        LinkHandshakeStageV1::SendLocalBinding,
        LinkHandshakeStageV1::SignHello,
        LinkHandshakeStageV1::PersistHello,
        LinkHandshakeStageV1::SendHello,
        LinkHandshakeStageV1::AwaitPeerHello,
        LinkHandshakeStageV1::ReadPeerControlBytes,
        LinkHandshakeStageV1::AwaitPeerHelloSignature,
        LinkHandshakeStageV1::PersistPeerHello,
        LinkHandshakeStageV1::PersistNegotiatedResult,
        LinkHandshakeStageV1::SignReady,
        LinkHandshakeStageV1::PersistReady,
        LinkHandshakeStageV1::SendReady,
        LinkHandshakeStageV1::AwaitPeerReady,
        LinkHandshakeStageV1::ReadPeerControlBytes,
        LinkHandshakeStageV1::AwaitPeerReadySignature,
        LinkHandshakeStageV1::PersistPeerReady,
        LinkHandshakeStageV1::SignAck,
        LinkHandshakeStageV1::PersistAck,
        LinkHandshakeStageV1::SendAck,
        LinkHandshakeStageV1::AwaitPeerAck,
        LinkHandshakeStageV1::ReadPeerControlBytes,
        LinkHandshakeStageV1::AwaitPeerAckSignature,
        LinkHandshakeStageV1::PersistPeerAck,
        LinkHandshakeStageV1::Connected}};
    return value;
}

/* ---------------------------------------------------------------- tests -- */

/*
 * gap 4, the headline test: the two public schedulers close the link over a real
 * bidirectional Control stream, with NO manual accept_peer_* injection anywhere.
 * Every HELLO/READY/ACK the peer sees is an exact framed record this code
 * produced, deframed through wire::next_app_frame() on the receiving side.
 */
void control_stream_loopback_closes_the_lobby()
{
    auto pair = make_pair(kGeneration, 41);

    const auto loop = run_control_loopback(pair, 12);
    check(!loop.stalled,
          "the Control stream loopback finished instead of stalling");

    check(pair.a.scheduler.connected() && pair.b.scheduler.connected(),
          "both sides reach CONNECTED_LOBBY over the Control stream alone");
    check(pair.a.scheduler.projected_state() ==
                  link::LinkControlStateV1::ConnectedLobby &&
              pair.b.scheduler.projected_state() ==
                  link::LinkControlStateV1::ConnectedLobby,
          "the contract projection is CONNECTED_LOBBY on both sides");

    /* Each side really opened its own stream and wrote exactly three messages. */
    check(pair.a.scheduler.control_stream() != 0 &&
              pair.b.scheduler.control_stream() != 0,
          "each side owns a Control stream handle");
    /* The two sides run on separate providers, so their handle values are
     * provider-scoped and may coincide; the handle each side uses must however be
     * the one ITS OWN open/accept answered with. */
    check(pair.a.scheduler.control_stream() == 0x4000 &&
              pair.b.scheduler.control_stream() == 0x4100,
          "each side uses exactly the handle its own OpenControlStream returned");
    check(pair.a.send_count == 4 && pair.b.send_count == 4 &&
              count_kind(pair.a, LinkHandshakeEffectKind::SendLocalBinding) == 1 &&
              count_kind(pair.b, LinkHandshakeEffectKind::SendLocalBinding) == 1,
          "each side published its 0x0212 binding and then HELLO, READY and ACK");
    check(count_kind(pair.a, LinkHandshakeEffectKind::SendHello) == 1 &&
              count_kind(pair.a, LinkHandshakeEffectKind::SendReady) == 1 &&
              count_kind(pair.a, LinkHandshakeEffectKind::SendAck) == 1,
          "exactly one HELLO, one READY and one ACK per side");
    /*
     * The peer binding was NOT pre-installed: it arrived as a real 0x0212 frame
     * on the Control stream, was deframed and codec-checked, and only then
     * installed. This is gap 3's inbound half actually running.
     */
    check(pair.a.scheduler.peer_binding_accepted() &&
              pair.b.scheduler.peer_binding_accepted(),
          "each side accepted the peer 0x0212 binding it received on Control");
    check(pair.a.scheduler.peer_binding().hash == pair.b.binding_hash &&
              pair.b.scheduler.peer_binding().hash == pair.a.binding_hash,
          "each side installed the peer's real binding hash, decoded not supplied");
    check(pair.a.scheduler.peer_binding().session_signing_public_key ==
              pair.b.session_public &&
              pair.b.scheduler.peer_binding().session_signing_public_key ==
                  pair.a.session_public,
          "each side installed the peer session key that binding authenticated");
    check(count_kind(pair.a, LinkHandshakeEffectKind::OpenControlStream) == 1 &&
              count_kind(pair.b, LinkHandshakeEffectKind::OpenControlStream) == 1,
          "each side opened the Control stream exactly once");
    check(count_kind(pair.a, LinkHandshakeEffectKind::ReadControlBytes) == 4 &&
              count_kind(pair.b, LinkHandshakeEffectKind::ReadControlBytes) == 4,
          "each side granted Control read credit once per awaited object: the "
          "peer binding, HELLO, READY and ACK");

    /* Every frame was handed over and fully consumed: nothing is left buffered,
     * which only happens when the deframer consumed whole records. */
    check(pair.a.inbox.empty() && pair.b.inbox.empty() &&
              pair.a.outbox.empty() && pair.b.outbox.empty() &&
              pair.a.scheduler.control_read_buffer().empty() &&
              pair.b.scheduler.control_read_buffer().empty(),
          "no undelivered or half-parsed Control bytes remain");

    /* The inbound path really ran the codec, not a shortcut. */
    check(pair.a.scheduler.peer_hello().object_hash ==
              pair.b.scheduler.local_hello().object_hash &&
              pair.b.scheduler.peer_hello().object_hash ==
                  pair.a.scheduler.local_hello().object_hash,
          "each side verified the peer HELLO it received over the Control stream");
    check(pair.a.scheduler.peer_ready().object_hash ==
              pair.b.scheduler.local_ready().object_hash &&
              pair.a.scheduler.peer_ack().object_hash ==
                  pair.b.scheduler.local_ack().object_hash,
          "each side verified the peer READY and ACK it received");

    const auto& expected = expected_sequence();
    check(pair.a.scheduler.stage_trace_size() == expected.size(),
          "the loopback initiator trace has exactly the frozen number of stages");
    for (std::size_t index = 0; index < expected.size(); ++index)
        check(pair.a.scheduler.stage_trace_at(index) == expected[index],
              "the loopback initiator stage trace follows the frozen legal order");
}

/*
 * A Control frame is deframed incrementally: a record split across two reads must
 * be buffered and completed, never mis-parsed or dropped.
 */
void control_frames_deframe_incrementally()
{
    auto pair = make_pair(kGeneration, 41);
    bool blocked = false;
    pump(pair.a, &blocked);
    check(blocked && pair.a.scheduler.awaiting() ==
                         LinkHandshakeStageV1::AwaitPeerHello,
          "the initiator is waiting for Control bytes after sending its HELLO");

    /* The responder's real HELLO frame, delivered one byte short. */
    pump(pair.b, &blocked);
    check(pair.b.outbox.size() == 2 &&
              pair.b.outbox.front().size() == 6u + 312u,
          "the responder published its 0x0212 binding and then one HELLO frame");
    /* The HELLO is the second record: the binding is published first. */
    const auto frame = pair.b.outbox[1];
    pair.b.outbox.clear();
    const std::vector<std::uint8_t> first(frame.begin(), frame.end() - 1);
    pair.a.inbox.push_back(first);
    pump(pair.a, &blocked);
    check(blocked && !pair.a.scheduler.failed(),
          "a half-delivered frame is buffered, not rejected");
    check(pair.a.scheduler.control_read_buffer().size() == frame.size() - 1,
          "the partial record is retained byte for byte");
    check(pair.a.scheduler.awaiting() == LinkHandshakeStageV1::AwaitPeerHello,
          "the attempt is still awaiting the peer HELLO");

    /* The last byte completes the record and the handshake advances. */
    pair.a.inbox.push_back(std::vector<std::uint8_t>{frame.back()});
    pump(pair.a, &blocked);
    check(!pair.a.scheduler.failed() &&
              pair.a.scheduler.control_read_buffer().empty(),
          "the final byte completes the record and clears the buffer");
    check(pair.a.scheduler.peer_hello().object_hash ==
              pair.b.scheduler.local_hello().object_hash,
          "the reassembled frame is the peer's real HELLO");
}

/*
 * The Control read pipeline is a gate, not a bypass: bytes that are not a
 * well-formed link control record fail the attempt closed. Nothing here reaches
 * accept_peer_hello, because app_frame refuses the frame first.
 */
void control_stream_refuses_malformed_frames()
{
    /* A registered link tag with a body that is not the object it claims. */
    auto pair = make_pair(kGeneration, 41);
    bool blocked = false;
    pump(pair.a, &blocked);

    std::vector<std::uint8_t> short_body(64, 0x5a);
    std::vector<std::uint8_t> framed(6u + short_body.size(), 0);
    std::size_t written = 0;
    check(wire::encode_app_frame(link::kLinkHelloObjectKindV1,
                                 short_body.data(), short_body.size(),
                                 framed.data(), framed.size(),
                                 &written) == wire::Status::Ok,
          "a short 0x0216 frame still encodes");
    pair.a.inbox.push_back(framed);
    pump(pair.a, &blocked);
    check(pair.a.scheduler.failed(),
          "a 0x0216 frame whose body is not a LINK_HELLO fails the link closed");
    check(!pair.a.scheduler.connected(),
          "a malformed Control frame never reaches CONNECTED_LOBBY");

    /*
     * A tag outside the link control plane is refused even though the tag itself
     * is registered: 0x0212 (SessionSigningKeyBindingV1) IS legal on the Control
     * CHANNEL, so it is the routing gate rather than the allow-list that must
     * reject it here.
     */
    auto other = make_pair(kGeneration, 41);
    pump(other.a, &blocked);
    std::vector<std::uint8_t> binding_body(wire::kSessionSigningBindingSizeV1,
                                           0x11);
    std::vector<std::uint8_t> binding_frame(6u + binding_body.size(), 0);
    check(wire::encode_app_frame(wire::kSessionSigningBindingObjectKindV1,
                                 binding_body.data(), binding_body.size(),
                                 binding_frame.data(), binding_frame.size(),
                                 &written) == wire::Status::Ok,
          "a 0x0212 frame encodes");
    other.a.inbox.push_back(binding_frame);
    pump(other.a, &blocked);
    check(other.a.scheduler.failed(),
          "a registered but non-link object on the link Control stream fails the "
          "link closed");
}

/* Both sides send their HELLO, verify the peer HELLO and produce their READY,
 * leaving both at AwaitPeerReady. */
void mutual_hello_exchange(Pair& pair)
{
    pump(pair.a);
    pump(pair.b);
    check(pair.a.scheduler.accept_peer_hello(
              pair.b.scheduler.local_hello_bytes().data(),
              pair.b.scheduler.local_hello_bytes().size()) == FLY_SESSION_V2_OK,
          "initiator accepts the responder HELLO");
    check(pair.b.scheduler.accept_peer_hello(
              pair.a.scheduler.local_hello_bytes().data(),
              pair.a.scheduler.local_hello_bytes().size()) == FLY_SESSION_V2_OK,
          "responder accepts the initiator HELLO");
    pump(pair.a);
    pump(pair.b);
}

void two_sided_legal_sequence()
{
    auto pair = make_pair(kGeneration, 41);
    check(pair.a.scheduler.begun() && pair.b.scheduler.begun(),
          "both sides begin");

    pump(pair.a);
    pump(pair.b);
    check(pair.a.scheduler.awaiting() == LinkHandshakeStageV1::AwaitPeerHello &&
              pair.b.scheduler.awaiting() == LinkHandshakeStageV1::AwaitPeerHello,
          "both sides reach AwaitPeerHello after sending their HELLO");
    check(count_kind(pair.a, LinkHandshakeEffectKind::SendHello) == 1 &&
              count_kind(pair.b, LinkHandshakeEffectKind::SendHello) == 1 &&
              pair.a.send_count == 2 && pair.b.send_count == 2,
          "each side published its 0x0212 binding and exactly one HELLO");

    check(pair.a.scheduler.accept_peer_hello(
              pair.b.scheduler.local_hello_bytes().data(),
              pair.b.scheduler.local_hello_bytes().size()) == FLY_SESSION_V2_OK,
          "initiator accepts the responder HELLO");
    check(pair.b.scheduler.accept_peer_hello(
              pair.a.scheduler.local_hello_bytes().data(),
              pair.a.scheduler.local_hello_bytes().size()) == FLY_SESSION_V2_OK,
          "responder accepts the initiator HELLO");
    pump(pair.a);
    pump(pair.b);
    check(pair.a.scheduler.awaiting() == LinkHandshakeStageV1::AwaitPeerReady &&
              pair.b.scheduler.awaiting() == LinkHandshakeStageV1::AwaitPeerReady,
          "both sides reach AwaitPeerReady after sending their READY");
    check(pair.a.scheduler.projected_state() ==
              link::LinkControlStateV1::Connecting,
          "a one-sided READY keeps the projection at CONNECTING");
    check(!pair.a.scheduler.connected(),
          "a one-sided READY never reports connected");

    check(pair.a.scheduler.accept_peer_ready(
              pair.b.scheduler.local_ready_bytes().data(),
              pair.b.scheduler.local_ready_bytes().size()) == FLY_SESSION_V2_OK,
          "initiator accepts the responder READY");
    check(pair.b.scheduler.accept_peer_ready(
              pair.a.scheduler.local_ready_bytes().data(),
              pair.a.scheduler.local_ready_bytes().size()) == FLY_SESSION_V2_OK,
          "responder accepts the initiator READY");
    pump(pair.a);
    pump(pair.b);
    check(pair.a.scheduler.awaiting() == LinkHandshakeStageV1::AwaitPeerAck &&
              pair.b.scheduler.awaiting() == LinkHandshakeStageV1::AwaitPeerAck,
          "both sides reach AwaitPeerAck after sending their ACK");
    check(pair.a.scheduler.projected_state() ==
              link::LinkControlStateV1::Connecting,
          "a missing peer ACK still keeps the projection at CONNECTING");

    check(pair.a.scheduler.accept_peer_ack(
              pair.b.scheduler.local_ack_bytes().data(),
              pair.b.scheduler.local_ack_bytes().size()) == FLY_SESSION_V2_OK,
          "initiator accepts the responder ACK");
    check(pair.b.scheduler.accept_peer_ack(
              pair.a.scheduler.local_ack_bytes().data(),
              pair.a.scheduler.local_ack_bytes().size()) == FLY_SESSION_V2_OK,
          "responder accepts the initiator ACK");
    pump(pair.a);
    pump(pair.b);

    check(pair.a.scheduler.connected() && pair.b.scheduler.connected(),
          "both sides reach CONNECTED_LOBBY only after the full closure");
    check(pair.a.scheduler.projected_state() ==
                  link::LinkControlStateV1::ConnectedLobby &&
              pair.b.scheduler.projected_state() ==
                  link::LinkControlStateV1::ConnectedLobby,
          "the projection is the contract's CONNECTED_LOBBY on both sides");

    const auto& expected = manual_sequence();
    check(pair.a.scheduler.stage_trace_size() == expected.size(),
          "the manual-injection initiator trace has exactly the frozen number of "
          "stages");
    for (std::size_t index = 0; index < expected.size(); ++index)
        check(pair.a.scheduler.stage_trace_at(index) == expected[index],
              "the initiator stage trace follows the frozen legal order");
    check(pair.b.scheduler.stage_trace_size() == expected.size(),
          "the responder trace has exactly the frozen number of stages");
    for (std::size_t index = 0; index < expected.size(); ++index)
        check(pair.b.scheduler.stage_trace_at(index) == expected[index],
              "the responder stage trace follows the frozen legal order");

    /* Both ends used the same exact codec and cross-matched object hashes. */
    check(pair.a.scheduler.peer_hello().object_hash ==
              pair.b.scheduler.local_hello().object_hash,
          "the initiator verified the responder's own HELLO object hash");
    check(pair.b.scheduler.peer_hello().object_hash ==
              pair.a.scheduler.local_hello().object_hash,
          "the responder verified the initiator's own HELLO object hash");
    check(pair.a.scheduler.local_ready().peer_hello_object_hash ==
                  pair.b.scheduler.local_hello().object_hash &&
              pair.a.scheduler.local_ready().local_hello_object_hash ==
                  pair.a.scheduler.local_hello().object_hash,
          "the initiator READY binds its own and the verified peer HELLO");
    check(pair.a.scheduler.local_ready().peer_summary_hash == summary_b() &&
              pair.a.scheduler.local_ack().peer_summary_hash == summary_b(),
          "the ACK binds the same verified summary hashes as READY");
    check(pair.a.scheduler.local_ready().negotiated_result_hash ==
              pair.b.scheduler.local_ready().negotiated_result_hash,
          "both sides persisted the same negotiated result hash");

    /*
     * The negotiated result is DERIVED, not supplied (owner decision
     * 2026-09-16), so both sides must have produced the same 64-byte preimage
     * from their own local/peer HELLO object hashes ordered by their own pair
     * role. The initiator's preimage is (initiator HELLO, responder HELLO) and
     * the responder's is the same pair read the other way round.
     */
    const auto& a_preimage = pair.a.scheduler.negotiated_result_preimage();
    const auto& b_preimage = pair.b.scheduler.negotiated_result_preimage();
    std::array<std::uint8_t, 32> probe_hash{};
    check(a_preimage.size() == 64u &&
              b_preimage.size() == 64u,
          "each side derived a 64-byte negotiated result preimage");
    check(std::equal(a_preimage.begin(), a_preimage.end(), b_preimage.begin()),
          "the derived preimages are byte-identical on both sides");

    /* Each side orders the SAME two hashes by its own pair role, so the two
     * preimages must be byte-identical — that is precisely why the derived value
     * is a negotiation result both sides agree on without exchanging it. */
    check(a_preimage == b_preimage,
          "both roles order the same HELLO pair into the identical preimage");
    check(std::equal(a_preimage.begin(), a_preimage.begin() + 32,
                     pair.a.scheduler.local_hello_object_hash().begin()) &&
              std::equal(a_preimage.begin() + 32, a_preimage.end(),
                         pair.a.scheduler.peer_hello_object_hash().begin()),
          "the initiator preimage is its own HELLO hash then the peer's");
    check(std::equal(b_preimage.begin(), b_preimage.begin() + 32,
                     pair.b.scheduler.peer_hello_object_hash().begin()) &&
              std::equal(b_preimage.begin() + 32, b_preimage.end(),
                         pair.b.scheduler.local_hello_object_hash().begin()),
          "the responder preimage puts the initiator's HELLO hash first too");
    check(wire::negotiated_result_hash_v1(
              pair.a.scheduler.local_hello_object_hash(),
              pair.a.scheduler.peer_hello_object_hash(),
              &probe_hash) == wire::Status::Ok &&
              probe_hash == pair.a.scheduler.negotiated_result_hash(),
          "the initiator's READY binds exactly the hash of its derived preimage");
    check(wire::negotiated_result_hash_v1(
              pair.b.scheduler.peer_hello_object_hash(),
              pair.b.scheduler.local_hello_object_hash(),
              &probe_hash) == wire::Status::Ok &&
              probe_hash == pair.b.scheduler.negotiated_result_hash(),
          "the responder's READY binds the same value from the mirrored pair");
    check(pair.a.scheduler.local_ack().ready_phase ==
              link::LinkReadyPhaseV1::Ack,
          "the third message is an ACK");
    check(pair.a.scheduler.progress().local_binding_durable &&
              pair.a.scheduler.progress().local_hello_durable &&
              pair.a.scheduler.progress().peer_hello_verified &&
              pair.a.scheduler.progress().negotiated_result_durable &&
              pair.a.scheduler.progress().local_ready_durable &&
              pair.a.scheduler.progress().peer_ready_verified &&
              pair.a.scheduler.progress().peer_ack_received,
          "every closure condition is durably recorded");

    /* A HELLO must never carry a STREAM capability. */
    check(pair.a.scheduler.local_hello().capability_bits ==
              link::kLinkSupportedCapabilityMaskV1,
          "the emitted HELLO advertises DUAL only");
    check((pair.a.scheduler.local_hello().capability_bits &
           link::kLinkCapabilityStreamVideoV1) == 0 &&
              (pair.a.scheduler.local_hello().capability_bits &
               link::kLinkCapabilityStreamAudioV1) == 0,
          "no STREAM capability bit is advertised");
}

void persist_before_send_is_enforced()
{
    /* Happy-path ordering. */
    {
        auto pair = make_pair(kGeneration, 41);
        pump(pair.a);
        check(index_of(pair.a, LinkHandshakeEffectKind::PersistHelloObject) <
                  index_of(pair.a, LinkHandshakeEffectKind::SendHello),
              "HELLO is persisted before it is sent");
    }
    {
        auto pair = make_pair(kGeneration, 41);
        exchange_hellos(pair);
        pair.a.scheduler.accept_peer_hello(
            pair.b.scheduler.local_hello_bytes().data(),
            pair.b.scheduler.local_hello_bytes().size());
        pump(pair.a);
        check(index_of(pair.a,
                       LinkHandshakeEffectKind::PersistNegotiatedResult) <
                  index_of(pair.a, LinkHandshakeEffectKind::SendReady),
              "the negotiated result is durable before READY is sent");
        check(index_of(pair.a, LinkHandshakeEffectKind::PersistReadyObject) <
                  index_of(pair.a, LinkHandshakeEffectKind::SendReady),
              "READY is persisted before it is sent");
    }

    /* ObjectStore failure on the HELLO object: no HELLO may be sent. */
    {
        auto pair = make_pair(kGeneration, 41);
        pair.a.fail_kind = LinkHandshakeEffectKind::PersistHelloObject;
        pump(pair.a);
        check(pair.a.scheduler.failed(),
              "a failed HELLO object write fails the attempt");
        check(count_kind(pair.a, LinkHandshakeEffectKind::SendHello) == 0,
              "no HELLO is sent when its durable write failed");
        /* The 0x0212 publication is not a "control message" in this sense: it
         * carries the binding the HELLO depends on and is sent before it, so the
         * assertion is about HELLO/READY/ACK only. */
        check(pair.a.send_count == 1 &&
                  count_kind(pair.a, LinkHandshakeEffectKind::SendLocalBinding) == 1,
              "only the 0x0212 binding left the device, never a HELLO");
        check(!pair.a.scheduler.poll_effect().has_value(),
              "a failed attempt leaves no pending effect");
    }

    /* SecureStore failure on the negotiated result: no READY may be sent. */
    {
        auto pair = make_pair(kGeneration, 41);
        exchange_hellos(pair);
        pair.a.scheduler.accept_peer_hello(
            pair.b.scheduler.local_hello_bytes().data(),
            pair.b.scheduler.local_hello_bytes().size());
        pair.a.fail_kind = LinkHandshakeEffectKind::PersistNegotiatedResult;
        pump(pair.a);
        check(pair.a.scheduler.failed(),
              "a failed negotiated result write fails the attempt");
        check(count_kind(pair.a, LinkHandshakeEffectKind::SendReady) == 0,
              "no READY is sent when the negotiated result is not durable");
        check(pair.a.send_count == 2 &&
                  count_kind(pair.a, LinkHandshakeEffectKind::SendHello) == 1 &&
                  count_kind(pair.a, LinkHandshakeEffectKind::SendLocalBinding) == 1,
              "only the 0x0212 binding and the earlier HELLO had been sent");
    }

    /* ObjectStore failure on the READY object: no READY may be sent. */
    {
        auto pair = make_pair(kGeneration, 41);
        exchange_hellos(pair);
        pair.a.scheduler.accept_peer_hello(
            pair.b.scheduler.local_hello_bytes().data(),
            pair.b.scheduler.local_hello_bytes().size());
        pair.a.fail_kind = LinkHandshakeEffectKind::PersistReadyObject;
        pump(pair.a);
        check(pair.a.scheduler.failed(),
              "a failed READY object write fails the attempt");
        check(count_kind(pair.a, LinkHandshakeEffectKind::SendReady) == 0,
              "no READY is sent when its own durable write failed");
    }

    /* The local 0x0212 binding must be read and verified, never assumed. */
    {
        auto pair = make_pair(kGeneration, 41);
        pair.a.fail_kind = LinkHandshakeEffectKind::ReadLocalBindingObject;
        pump(pair.a);
        check(pair.a.scheduler.failed(),
              "an unreadable local binding fails the attempt");
        check(pair.a.send_count == 0,
              "nothing is sent without a durable local binding");
        check(!pair.a.scheduler.progress().local_binding_durable,
              "an unreadable binding is never marked durable");
        check(index_of(pair.a,
                       LinkHandshakeEffectKind::ReadLocalBindingObject) ==
                  index_of(pair.a,
                           LinkHandshakeEffectKind::OpenControlStream) + 1,
              "the durable binding is read immediately after the Control stream "
              "is opened, and before anything else");
    }
}

void peer_messages_are_durable_before_ack()
{
    auto pair = make_pair(kGeneration, 41);
    mutual_hello_exchange(pair);
    check(pair.a.scheduler.accept_peer_ready(
              pair.b.scheduler.local_ready_bytes().data(),
              pair.b.scheduler.local_ready_bytes().size()) == FLY_SESSION_V2_OK,
          "the peer READY is accepted");

    const auto before = static_cast<int>(pair.a.answered.size());
    pump(pair.a);
    /* The signature gate now sits immediately before the durable write, and the
     * ordering assertion keeps its full strength: the peer READY is still
     * persisted before this side ACKs it, only one effect later than before. */
    check(before + 1 < static_cast<int>(pair.a.answered.size()) &&
              pair.a.answered[static_cast<std::size_t>(before)] ==
                  LinkHandshakeEffectKind::VerifyPeerSignature &&
              pair.a.answered[static_cast<std::size_t>(before + 1)] ==
                  LinkHandshakeEffectKind::PersistPeerReadyObject,
          "the verified peer READY is persisted first");
    check(index_of(pair.a, LinkHandshakeEffectKind::PersistPeerReadyObject) <
              index_of(pair.a, LinkHandshakeEffectKind::SendAck),
          "the peer READY is durable before this side ACKs it");
    check(index_of(pair.a, LinkHandshakeEffectKind::PersistAckObject) <
              index_of(pair.a, LinkHandshakeEffectKind::SendAck),
          "this side's own ACK is durable before it is sent");

    /* A failed peer-READY write must not produce an ACK. */
    auto other = make_pair(kGeneration, 41);
    mutual_hello_exchange(other);
    other.a.scheduler.accept_peer_ready(
        other.b.scheduler.local_ready_bytes().data(),
        other.b.scheduler.local_ready_bytes().size());
    other.a.fail_kind = LinkHandshakeEffectKind::PersistPeerReadyObject;
    pump(other.a);
    check(other.a.scheduler.failed(),
          "an unpersisted peer READY fails the attempt");
    check(count_kind(other.a, LinkHandshakeEffectKind::SendAck) == 0,
          "no ACK is sent for a peer READY that could not be persisted");
}

void no_peer_ack_means_no_lobby()
{
    auto pair = make_pair(kGeneration, 41);
    mutual_hello_exchange(pair);
    pair.a.scheduler.accept_peer_ready(
        pair.b.scheduler.local_ready_bytes().data(),
        pair.b.scheduler.local_ready_bytes().size());
    pump(pair.a);
    check(pair.a.scheduler.awaiting() == LinkHandshakeStageV1::AwaitPeerAck,
          "the initiator is waiting for the peer ACK");
    check(!pair.a.scheduler.connected() &&
              pair.a.scheduler.projected_state() ==
                  link::LinkControlStateV1::Connecting,
          "without the peer ACK the link stays CONNECTING");
    check(!pair.a.scheduler.progress().peer_ack_received,
          "the missing peer ACK is not recorded as received");
}

void third_party_codec_agrees()
{
    /* Decode the scheduler's own output with the standalone codec, using
     * hand-built expectations, to prove both ends share one exact codec. */
    auto pair = make_pair(kGeneration, 41);
    pump(pair.a);
    pump(pair.b);

    wire::LinkHelloExpectationsV1 expected{};
    expected.session_id = session_id();
    expected.link_id = link_id();
    expected.channel_id = kChannelId;
    expected.connection_generation = kGeneration;
    expected.link_generation = kLinkGeneration;
    expected.local_role = wire::PairRoleV1::Initiator;
    expected.selected_plan_hash = plan_hash();
    expected.endpoint_offer_hash = offer_hash();
    expected.pair_transcript_object_hash = transcript_object_hash();
    expected.peer_binding_hash = pair.b.binding_hash;
    expected.peer_identity_key_id = pair.b.identity_key_id;
    expected.peer_identity_public_key = pair.b.identity_public;
    expected.peer_session_signing_public_key = pair.b.session_public;
    wire::LinkControlDecodeReportV1 report{};
    link::LinkHelloV1 value{};
    const auto& bytes = pair.b.scheduler.local_hello_bytes();
    check(wire::decode_link_hello_v1(
              bytes.data(), bytes.size(), expected,
              wire::validate_p256_uncompressed_point_callback, nullptr,
              test_verify, nullptr, &report, &value) == wire::Status::Ok,
          "the responder HELLO decodes under hand-built initiator expectations");
    check(value.object_hash == pair.b.scheduler.local_hello().object_hash,
          "the standalone codec reproduces the scheduler's object hash");
    check(value.digest == pair.b.scheduler.local_hello().digest,
          "the standalone codec reproduces the scheduler's digest");
}

} // namespace

/*
 * gap 3: the peer's accepted 0x0212 binding is the only owner of
 * peer_binding_hash / peer_identity_key_id / peer_session_signing_public_key.
 * These checks drive accept_peer_binding() directly with the peer side's real
 * 312 bytes, and with every hostile variant of them.
 */
void peer_binding_has_a_real_owner()
{
    auto pair = make_pair(kGeneration, 41);

    /* Before any binding is installed there is no peer material at all. */
    check(!pair.a.scheduler.peer_binding_accepted(),
          "an attempt starts with no accepted peer binding");

    /* The responder's genuine binding, installed on the initiator. */
    check(pair.a.scheduler.accept_peer_binding(
              pair.b.binding.data(), pair.b.binding.size(),
              pair.b.binding_hash) == FLY_SESSION_V2_OK,
          "the peer's genuine 0x0212 binding is accepted by hash");
    check(pair.a.scheduler.peer_binding_accepted(),
          "an accepted peer binding is reported as accepted");
    check(pair.a.scheduler.peer_binding().hash == pair.b.binding_hash &&
              pair.a.scheduler.peer_binding().identity_key_id ==
                  pair.b.identity_key_id &&
              pair.a.scheduler.peer_binding().session_signing_public_key ==
                  pair.b.session_public,
          "every peer field is derived from the decoded binding, not supplied");

    /* A different hash must be refused: the binding is pinned by the hash the
     * peer's own HELLO will name. */
    auto fresh = make_pair(kGeneration, 41);
    auto wrong_hash = pair.b.binding_hash;
    wrong_hash[0] ^= 0xffu;
    check(fresh.a.scheduler.accept_peer_binding(
              pair.b.binding.data(), pair.b.binding.size(), wrong_hash) ==
              FLY_SESSION_V2_AUTH_FAILED,
          "a peer binding whose content hash does not match is refused");
    check(!fresh.a.scheduler.peer_binding_accepted(),
          "a refused peer binding installs nothing");

    /* Truncated / trailing / tampered bytes all fail closed. */
    fresh = make_pair(kGeneration, 41);
    check(fresh.a.scheduler.accept_peer_binding(
              pair.b.binding.data(), pair.b.binding.size() - 1,
              pair.b.binding_hash) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "a truncated peer binding is refused");
    fresh = make_pair(kGeneration, 41);
    check(fresh.a.scheduler.accept_peer_binding(
              nullptr, pair.b.binding.size(), pair.b.binding_hash) ==
              FLY_SESSION_V2_INVALID_ARGUMENT,
          "a null peer binding is refused");
    fresh = make_pair(kGeneration, 41);
    auto tampered = pair.b.binding;
    tampered[176] ^= 0x01u; /* the session signing key inside the binding */
    check(fresh.a.scheduler.accept_peer_binding(
              tampered.data(), tampered.size(), pair.b.binding_hash) ==
              FLY_SESSION_V2_AUTH_FAILED,
          "a tampered peer binding fails its own content hash");
    check(!fresh.a.scheduler.peer_binding_accepted(),
          "a tampered peer binding installs nothing");

    auto hostile = pair;
    hostile.a.scheduler.accept_peer_binding(pair.b.binding.data(),
                                            pair.b.binding.size(),
                                            pair.b.binding_hash);
    check(hostile.a.scheduler.peer_binding().session_signing_public_key ==
              pair.b.session_public,
          "the installed peer session signing key is the binding's own");
}

int main()
{
    control_stream_loopback_closes_the_lobby();
    control_frames_deframe_incrementally();
    control_stream_refuses_malformed_frames();
    two_sided_legal_sequence();
    persist_before_send_is_enforced();
    peer_messages_are_durable_before_ack();
    no_peer_ack_means_no_lobby();
    third_party_codec_agrees();
    peer_binding_has_a_real_owner();

    if (failures != 0) {
        std::fprintf(stderr,
                     "%d link handshake scheduler checks failed\n", failures);
        return 1;
    }
    std::puts("link handshake scheduler tests passed");
    return 0;
}
