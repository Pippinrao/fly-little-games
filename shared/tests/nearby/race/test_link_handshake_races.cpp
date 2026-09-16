#include "link/link_handshake_scheduler.hpp"
#include "buffer_handle.hpp"
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
 * W1 Task 4 race / cancellation matrix for the link handshake scheduler.
 * Every case asserts both the returned result and the absence of side effects:
 * a stale, duplicated or late message may never advance the live link.
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

template <std::size_t N>
std::array<std::uint8_t, N> filled(std::uint8_t value)
{
    std::array<std::uint8_t, N> out{};
    out.fill(value);
    return out;
}

const std::array<std::uint8_t, 16>& session_id()
{
    static const auto value = filled<16>(0x11);
    return value;
}
const std::array<std::uint8_t, 16>& link_id()
{
    static const auto value = filled<16>(0x22);
    return value;
}
const std::array<std::uint8_t, 32>& transcript_hash()
{
    static const auto value = filled<32>(0x55);
    return value;
}
constexpr std::array<std::uint8_t, 16> kChannelId = {{
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};
constexpr std::uint64_t kGeneration = 7;
constexpr std::uint64_t kLinkGeneration = 9;

struct Side
{
    LinkHandshakeScheduler scheduler;
    LinkHandshakeStartV1 start{};
    std::array<std::uint8_t, wire::kSessionSigningBindingSizeV1> binding{};
    std::array<std::uint8_t, 32> binding_hash{};
    std::array<std::uint8_t, 65> identity_public{};
    std::array<std::uint8_t, 65> session_public{};
    /* No negotiated-result fixture: the scheduler derives it from the two HELLO
     * object hashes, so a test-side copy would be a competing definition. */
    std::vector<LinkHandshakeEffectKind> answered{};
    int send_count = 0;
    /* gap 4 loopback transport. */
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
                                         std::uint32_t kind)
{
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = 1;
    payload.generation = token.connection_generation;
    auto event = end_event(token, kind, FLY_SESSION_V2_OK);
    event.payload_kind = kind;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

/* gap 4: the two-handle terminal an open_bidi/accept_bidi answers with. */
fly_session_port_event_v2 stream_event(const fly_session_op_token_v2& token,
                                       fly_session_resource_handle_v2 send_stream,
                                       fly_session_resource_handle_v2 receive_stream)
{
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = send_stream;
    payload.generation = token.connection_generation;
    payload.value0 = receive_stream;
    auto event = end_event(token, FLY_SESSION_PROVIDER_QUIC_STREAM_V2,
                           FLY_SESSION_V2_OK);
    event.payload_kind = FLY_SESSION_PROVIDER_QUIC_STREAM_V2;
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
    auto event = end_event(token, FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2,
                           FLY_SESSION_V2_OK);
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
        }
    }
    const auto result = scheduler.complete(event);
    if (buffer != nullptr) fly_session_buffer_release_v2(buffer);
    return result;
}

fly_session_result_v2 answer(Side& side, const LinkHandshakeEffect& effect)
{
    side.answered.push_back(effect.kind);
    switch (effect.kind) {
    case LinkHandshakeEffectKind::OpenControlStream: {
        /* gap 4: the two-handle stream terminal, same shape the bind used. */
        const auto send = side.next_stream_handle++;
        const auto receive = side.next_stream_handle++;
        return complete_event(side.scheduler,
                              stream_event(effect.token, send, receive));
    }
    case LinkHandshakeEffectKind::ReadControlBytes: {
        /* gap 4: deliver whatever the loopback buffered. pump() never calls this
         * with an empty inbox, so a read can never be silently completed with no
         * bytes. */
        if (side.inbox.empty()) return FLY_SESSION_V2_INVALID_STATE;
        auto bytes = std::move(side.inbox.front());
        side.inbox.erase(side.inbox.begin());
        return complete_event(
            side.scheduler,
            buffer_event(effect.token, FLY_SESSION_PROVIDER_QUIC_DATA_V2,
                         bytes.data(), bytes.size()));
    }
    case LinkHandshakeEffectKind::ReadLocalBindingObject:
        return complete_event(
            side.scheduler,
            hash_event(effect.token, side.binding.data(), side.binding.size(),
                       side.binding_hash));
    case LinkHandshakeEffectKind::SignHello:
    case LinkHandshakeEffectKind::SignReady:
    case LinkHandshakeEffectKind::SignAck: {
        const auto signature = test_sign(side.session_public, effect.digest);
        return complete_event(
            side.scheduler,
            buffer_event(effect.token, FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2,
                         signature.data(), signature.size()));
    }
    case LinkHandshakeEffectKind::VerifyPeerSignature: {
        /* The engine's asynchronous crypto terminal, driven by the same real
         * verification the provider performs. */
        const bool verified =
            test_verify(nullptr, effect.signer_public_key.data(),
                        effect.digest.data(), effect.signature.data());
        return complete_event(
            side.scheduler,
            end_event(effect.token, FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2,
                      verified ? FLY_SESSION_V2_OK : FLY_SESSION_V2_AUTH_FAILED));
    }
    case LinkHandshakeEffectKind::PersistHelloObject:
    case LinkHandshakeEffectKind::PersistPeerHelloObject:
    case LinkHandshakeEffectKind::PersistReadyObject:
    case LinkHandshakeEffectKind::PersistPeerReadyObject:
    case LinkHandshakeEffectKind::PersistAckObject:
    case LinkHandshakeEffectKind::PersistPeerAckObject:
        return complete_event(
            side.scheduler,
            hash_event(effect.token, effect.value.data(), effect.value.size(),
                       effect.expected_hash));
    case LinkHandshakeEffectKind::PersistNegotiatedResult:
        return complete_event(
            side.scheduler,
            resource_event(effect.token,
                           FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2));
    case LinkHandshakeEffectKind::SendHello:
    case LinkHandshakeEffectKind::SendReady:
    case LinkHandshakeEffectKind::SendAck:
    case LinkHandshakeEffectKind::SendLocalBinding:
        ++side.send_count;
        /* gap 4: the exact framed record goes into the loopback outbox. */
        side.outbox.push_back(effect.value);
        return complete_event(side.scheduler,
                              end_event(effect.token, effect.expected_payload_kind, FLY_SESSION_V2_OK));
    }
    return FLY_SESSION_V2_INVALID_STATE;
}

int pump(Side& side, bool* blocked = nullptr)
{
    if (blocked != nullptr) *blocked = false;
    int answered = 0;
    while (true) {
        const auto effect = side.scheduler.poll_effect();
        if (!effect) break;
        /* gap 4: a Control read needs peer bytes; report blocked and let the
         * driver give the other side a turn instead of stalling here. */
        if (effect->kind == LinkHandshakeEffectKind::ReadControlBytes &&
            side.inbox.empty()) {
            if (blocked != nullptr) *blocked = true;
            break;
        }
        const auto result = answer(side, *effect);
        ++answered;
        if (result != FLY_SESSION_V2_OK) break;
    }
    return answered;
}

Side make_side(wire::PairRoleV1 role,
               const std::array<std::uint8_t, 65>& identity,
               const std::array<std::uint8_t, 65>& session,
               const std::array<std::uint8_t, 65>& peer_identity,
               const std::array<std::uint8_t, 65>& peer_session,
               const std::array<std::uint8_t, 32>& peer_binding_hash,
               std::uint64_t first_operation_id, std::uint64_t generation)
{
    Side side;
    side.identity_public = identity;
    side.session_public = session;

    std::array<std::uint8_t, wire::kSessionSigningBindingPretagSizeV1> pretag{};
    std::array<std::uint8_t, 32> digest{};
    check(wire::build_session_signing_binding_pretag_v1(
              transcript_hash(), session_id(), role, identity, session,
              wire::validate_p256_uncompressed_point_callback, nullptr, &pretag,
              &digest) == wire::Status::Ok,
          "fixture builds the local 0x0212 binding pretag");
    check(wire::finish_session_signing_binding_v1(
              pretag, test_sign(session, digest), &side.binding,
              &side.binding_hash) == wire::Status::Ok,
          "fixture persists a canonical local 0x0212 binding");

    LinkHandshakeStartV1 start{};
    start.engine_instance_id.fill(0x31);
    start.link_id = link_id();
    start.session_id = session_id();
    start.generation = generation;
    start.link_generation = kLinkGeneration;
    start.channel_id = kChannelId;
    start.first_operation_id = first_operation_id;
    start.local_role = role;
    start.pair_transcript_hash = transcript_hash();
    start.pair_transcript_object_hash = filled<32>(0x56);
    start.selected_plan_hash = filled<32>(0x33);
    start.endpoint_offer_hash = filled<32>(0x44);
    start.channel_bind_hash = filled<32>(0x88);
    start.local_binding_hash = side.binding_hash;
    start.local_identity_public_key = identity;
    start.local_session_signing_public_key = session;
    start.peer_binding_hash = peer_binding_hash;
    start.peer_identity_key_id = wire::link_identity_key_id_v1(
        peer_identity.data());
    start.peer_identity_public_key = peer_identity;
    start.peer_session_signing_public_key = peer_session;
    start.local_summary_hash = filled<32>(0xbb);
    start.peer_summary_hash = filled<32>(0xcc);
    start.merge_result_hash = filled<32>(0xdd);
    start.session_signing_key = 0x1000;
    start.quic_connection = 0x3000;
    start.local_is_listener = role == wire::PairRoleV1::Responder;
    side.start = start;
    check(side.scheduler.begin(side.start), "scheduler begins a link attempt");
    return side;
}

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
    if (wire::finish_session_signing_binding_v1(
            pretag, test_sign(session, digest), &binding, &hash) !=
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
    const auto a_hash = binding_hash_for(wire::PairRoleV1::Initiator, g2, g1);
    const auto b_hash = binding_hash_for(wire::PairRoleV1::Responder, g1, g2);
    Pair pair{make_side(wire::PairRoleV1::Initiator, g2, g1, g1, g2, b_hash,
                        first_operation_id, generation),
              make_side(wire::PairRoleV1::Responder, g1, g2, g2, g1, a_hash,
                        first_operation_id + 2, generation)};
    return pair;
}

/* A peer HELLO whose content the test mutates, re-signed by the peer session
 * key, which is exactly what a hostile or confused peer would produce. */
template <typename Mutator>
std::array<std::uint8_t, link::kLinkHelloSizeV1> resigned_hello(
    const Side& peer, Mutator mutate)
{
    auto value = peer.scheduler.local_hello();
    mutate(value);
    std::array<std::uint8_t, link::kLinkHelloPretagSizeV1> pretag{};
    std::array<std::uint8_t, 32> digest{};
    check(wire::build_link_hello_pretag_v1(
              value, wire::validate_p256_uncompressed_point_callback, nullptr,
              &pretag, &digest) == wire::Status::Ok,
          "fixture rebuilds a mutated peer HELLO pretag");
    const auto signature = test_sign(peer.session_public, digest);
    std::array<std::uint8_t, link::kLinkHelloSizeV1> bytes{};
    link::LinkHelloV1 parsed{};
    check(wire::finish_link_hello_v1(pretag, signature, &bytes, &parsed) ==
              wire::Status::Ok,
          "fixture signs the mutated peer HELLO");
    return bytes;
}

/* ---------------------------------------------------------------- tests -- */

void ready_before_hello_is_inert()
{
    auto pair = make_pair(kGeneration, 41);
    pump(pair.a);
    pump(pair.b);
    const auto stage = pair.a.scheduler.stage();
    const auto progress = pair.a.scheduler.progress();
    const auto& ready = pair.b.scheduler.local_ready_bytes();

    check(pair.a.scheduler.accept_peer_ready(ready.data(), ready.size()) ==
              FLY_SESSION_V2_INVALID_STATE,
          "a READY that arrives before HELLO is rejected as out of order");
    check(pair.a.scheduler.stage() == stage,
          "an out-of-order READY does not change the stage");
    check(!pair.a.scheduler.poll_effect().has_value() ||
              pair.a.scheduler.poll_effect()->kind ==
                  LinkHandshakeEffectKind::ReadControlBytes,
          "an out-of-order READY does not displace the pending Control read");
    check(pair.a.scheduler.progress().peer_ready_verified ==
              progress.peer_ready_verified,
          "an out-of-order READY does not record a verified peer");
    check(!pair.a.scheduler.failed(),
          "an out-of-order READY does not fail the live link");

    const auto& hello = pair.b.scheduler.local_hello_bytes();
    check(pair.a.scheduler.accept_peer_hello(hello.data(), hello.size()) ==
              FLY_SESSION_V2_OK,
          "the legal HELLO still succeeds after an out-of-order READY");
    pump(pair.a);
    check(pair.a.scheduler.awaiting() == LinkHandshakeStageV1::AwaitPeerReady,
          "the link advances normally after the rejected READY");
}

void stale_generation_is_dropped()
{
    auto pair = make_pair(kGeneration, 41);
    pump(pair.a);
    pump(pair.b);
    const auto stale = resigned_hello(pair.b, [](link::LinkHelloV1& value) {
        value.connection_generation = kGeneration - 1;
    });
    const auto stage = pair.a.scheduler.stage();

    check(pair.a.scheduler.accept_peer_hello(stale.data(), stale.size()) ==
              FLY_SESSION_V2_STALE,
          "a HELLO from an older connection generation is reported stale");
    check(pair.a.scheduler.stage() == stage && !pair.a.scheduler.failed(),
          "a stale HELLO does not fail or advance the live link");
    check(!pair.a.scheduler.progress().peer_hello_verified,
          "a stale HELLO never marks the peer verified");
    check(!pair.a.scheduler.poll_effect().has_value() ||
              pair.a.scheduler.poll_effect()->kind ==
                  LinkHandshakeEffectKind::ReadControlBytes,
          "a stale HELLO does not displace the pending Control read");

    const auto& hello = pair.b.scheduler.local_hello_bytes();
    check(pair.a.scheduler.accept_peer_hello(hello.data(), hello.size()) ==
              FLY_SESSION_V2_OK,
          "the current-generation HELLO is still accepted");
}

void duplicate_hello_is_idempotent()
{
    auto pair = make_pair(kGeneration, 41);
    pump(pair.a);
    pump(pair.b);
    const auto& hello = pair.b.scheduler.local_hello_bytes();
    check(pair.a.scheduler.accept_peer_hello(hello.data(), hello.size()) ==
              FLY_SESSION_V2_OK,
          "the peer HELLO is accepted once");
    const auto effects_after_first =
        static_cast<int>(pair.a.answered.size());
    pump(pair.a);
    const auto effects_after_persist =
        static_cast<int>(pair.a.answered.size());

    check(pair.a.scheduler.accept_peer_hello(hello.data(), hello.size()) ==
              FLY_SESSION_V2_DUPLICATE,
          "replaying the exact same HELLO bytes is reported as a duplicate");
    check(static_cast<int>(pair.a.answered.size()) == effects_after_persist,
          "a duplicate HELLO schedules no new effect");
    check(effects_after_persist > effects_after_first,
          "the first delivery did schedule its durable write");

    /* The same conversation with different bytes is a protocol violation. */
    const auto other = resigned_hello(pair.b, [](link::LinkHelloV1& value) {
        value.selected_plan_hash.fill(0x77);
    });
    check(pair.a.scheduler.accept_peer_hello(other.data(), other.size()) !=
              FLY_SESSION_V2_OK,
          "a second, different HELLO in the same attempt is rejected");
}

void operation_token_and_payload_kind_are_enforced()
{
    auto pair = make_pair(kGeneration, 41);
    const auto effect = pair.a.scheduler.poll_effect();
    check(effect.has_value(), "the first effect is pending");
    if (!effect) return;
    const auto stage = pair.a.scheduler.stage();

    /* Same token, wrong payload kind. */
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    auto wrong_kind = end_event(effect->token, effect->expected_payload_kind, FLY_SESSION_V2_OK);
    wrong_kind.payload_kind = FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2;
    wrong_kind.payload_size = sizeof(payload);
    std::memcpy(wrong_kind.payload, &payload, sizeof(payload));
    check(complete_event(pair.a.scheduler, wrong_kind) ==
              FLY_SESSION_V2_CONTRACT_VIOLATION,
          "a completion with the wrong payload kind is a contract violation");
    check(pair.a.scheduler.stage() == stage && !pair.a.scheduler.failed(),
          "the wrong payload kind leaves the pending operation untouched");

    /* A token from another operation id. */
    auto other_token = effect->token;
    other_token.operation_id = effect->token.operation_id + 1;
    const auto unknown = end_event(other_token, FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, FLY_SESSION_V2_OK);
    check(complete_event(pair.a.scheduler, unknown) == FLY_SESSION_V2_STALE,
          "a completion for an unknown operation id is stale");
    check(pair.a.scheduler.stage() == stage,
          "an unknown operation id does not advance the stage");

    /* The real completion still works. */
    const auto answered = answer(pair.a, *effect);
    check(answered == FLY_SESSION_V2_OK,
          "the correctly typed completion is accepted afterwards");
    check(pair.a.scheduler.stage() != stage,
          "the correct completion does advance the attempt");
}

void cancel_and_late_completion()
{
    /* Cancel with no pending operation is a no-op error. A begun attempt always
     * has the Control read outstanding at an await stage, so the only state with
     * truly nothing pending is a scheduler that never began. */
    {
        LinkHandshakeScheduler idle;
        check(idle.cancel_pending() == FLY_SESSION_V2_INVALID_STATE,
              "cancelling with nothing pending is rejected");
    }

    /* Cancel a live provider operation, then deliver its late terminal. */
    {
        auto pair = make_pair(kGeneration, 41);
        const auto effect = pair.a.scheduler.poll_effect();
        check(effect.has_value(), "a provider operation is pending");
        if (!effect) return;
        const auto late = end_event(effect->token, effect->expected_payload_kind, FLY_SESSION_V2_OK);
        check(pair.a.scheduler.cancel_pending() == FLY_SESSION_V2_OK,
              "a pending provider operation can be cancelled");
        check(pair.a.scheduler.failed(),
              "cancelling a live attempt fails it closed");
        check(!pair.a.scheduler.has_pending_operation(),
              "cancelling releases the journalled operation");
        check(complete_event(pair.a.scheduler, late) ==
                  FLY_SESSION_V2_INVALID_STATE,
              "a completion that arrives after cancellation is rejected");
        check(!pair.a.scheduler.poll_effect().has_value(),
              "a late completion schedules nothing");
    }

    /* A late terminal from the previous attempt after a restart is stale. */
    {
        auto pair = make_pair(kGeneration, 41);
        const auto old_effect = pair.a.scheduler.poll_effect();
        check(old_effect.has_value(), "the old attempt has a pending effect");
        if (!old_effect) return;
        const auto old_token = old_effect->token;
        const auto old_event = end_event(old_token, FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, FLY_SESSION_V2_OK);

        pair.a.start.generation = kGeneration + 1;
        pair.a.start.first_operation_id = 500;
        check(pair.a.scheduler.begin(pair.a.start),
              "a newer generation restarts the attempt");
        const auto fresh = pair.a.scheduler.poll_effect();
        check(fresh.has_value() &&
                  fresh->token.connection_generation == kGeneration + 1,
              "the restarted attempt owns a fresh generation-scoped token");
        check(fresh.has_value() &&
                  fresh->token.operation_id != old_token.operation_id,
              "restarting replaces the previous attempt's operation");
        check(pair.a.scheduler.has_pending_operation(),
              "the restarted attempt owns exactly one live operation");

        check(complete_event(pair.a.scheduler, old_event) ==
                  FLY_SESSION_V2_STALE,
              "a late completion from the replaced attempt is stale");
        check(fresh.has_value() &&
                  pair.a.scheduler.stage() ==
                      LinkHandshakeStageV1::OpenControl,
              "the new attempt is unaffected by the old completion");
        check(pair.a.scheduler.stage_trace_size() <
                  flynes::session::kLinkHandshakeTraceCapacityV1,
              "the restart did not overflow the stage trace");
        const auto restarted = pair.a.scheduler.poll_effect();
        check(restarted.has_value(),
              "the restarted attempt has its own pending effect");
        if (restarted)
            check(answer(pair.a, *restarted) == FLY_SESSION_V2_OK,
                  "the new attempt continues after the stale completion");
    }
}

void older_generation_restart_is_rejected()
{
    auto pair = make_pair(kGeneration, 41);
    pump(pair.a);
    const auto stage = pair.a.scheduler.stage();
    pair.a.start.generation = kGeneration - 1;
    check(!pair.a.scheduler.begin(pair.a.start),
          "an older generation may not replace a live attempt");
    check(pair.a.scheduler.stage() == stage,
          "a rejected restart leaves the live attempt untouched");
    check(!pair.a.scheduler.failed(),
          "a rejected restart does not fail the live attempt");
    pair.a.start.generation = kGeneration;
    check(!pair.a.scheduler.begin(pair.a.start),
          "the same generation may not replace a live attempt");
}

void terminal_and_shutdown_close_the_attempt()
{
    {
        auto pair = make_pair(kGeneration, 41);
        check(pair.a.scheduler.on_link_terminal(FLY_SESSION_V2_IO_FAILED) ==
                  FLY_SESSION_V2_IO_FAILED,
              "a QUIC terminal is reported back to the caller");
        check(pair.a.scheduler.failed() &&
                  pair.a.scheduler.projected_state() ==
                      link::LinkControlStateV1::Failed,
              "a QUIC terminal projects FAILED");
        check(!pair.a.scheduler.poll_effect().has_value(),
              "a terminated transport leaves nothing pending");
        check(pair.a.scheduler.on_link_terminal(FLY_SESSION_V2_IO_FAILED) ==
                  FLY_SESSION_V2_IO_FAILED,
              "a second terminal is answered without further side effects");
    }
    {
        auto pair = make_pair(kGeneration, 41);
        check(pair.a.scheduler.shutdown() == FLY_SESSION_V2_CLOSED,
              "shutdown closes the attempt");
        check(pair.a.scheduler.failed() &&
                  pair.a.scheduler.projected_state() ==
                      link::LinkControlStateV1::Failed,
              "shutdown projects FAILED");
        check(!pair.a.scheduler.has_pending_operation(),
              "shutdown releases the journalled operation");
        const auto& hello = pair.b.scheduler.local_hello_bytes();
        pump(pair.b);
        check(pair.a.scheduler.accept_peer_hello(hello.data(), hello.size()) ==
                  FLY_SESSION_V2_INVALID_STATE,
              "a closed attempt refuses inbound peer messages");
    }
    {
        auto pair = make_pair(kGeneration, 41);
        const auto effect = pair.a.scheduler.poll_effect();
        if (!effect) return;
        const auto event = end_event(effect->token, effect->expected_payload_kind, FLY_SESSION_V2_OK);
        pair.a.scheduler.on_link_terminal(FLY_SESSION_V2_TIMEOUT);
        check(complete_event(pair.a.scheduler, event) ==
                  FLY_SESSION_V2_INVALID_STATE,
              "a terminal delivered after the transport died is rejected");
    }
}

void repeated_attempts_do_not_leak()
{
    auto pair = make_pair(kGeneration, 41);
    for (int attempt = 0; attempt < 8; ++attempt) {
        const auto effect = pair.a.scheduler.poll_effect();
        if (effect) pair.a.scheduler.cancel_pending();
        pair.a.start.generation = kGeneration + 1 + static_cast<std::uint64_t>(attempt);
        pair.a.start.first_operation_id =
            100 + static_cast<std::uint64_t>(attempt) * 10;
        check(pair.a.scheduler.begin(pair.a.start),
              "a repeated attempt begins on a newer generation");
        check(pair.a.scheduler.has_pending_operation(),
              "every attempt registers exactly one live operation");
        pump(pair.a);
        check(!pair.a.scheduler.has_pending_journal_operation(),
              "completing an attempt releases its journalled operation");
        check(pair.a.scheduler.has_pending_operation() &&
                  pair.a.scheduler.poll_effect().has_value() &&
                  pair.a.scheduler.poll_effect()->kind ==
                      LinkHandshakeEffectKind::ReadControlBytes,
              "the only effect left outstanding is the Control read");
        check(!pair.a.scheduler.failed(),
              "a completed attempt is not marked failed");
    }
    check(pair.a.scheduler.awaiting() == LinkHandshakeStageV1::AwaitPeerHello,
          "the final attempt reached the peer-wait stage");
    check(pair.a.send_count == 16 &&
              std::count(pair.a.answered.begin(), pair.a.answered.end(),
                       LinkHandshakeEffectKind::SendLocalBinding) == 8 &&
              std::count(pair.a.answered.begin(), pair.a.answered.end(),
                         LinkHandshakeEffectKind::SendHello) == 8,
          "each attempt published its 0x0212 binding and exactly one HELLO, "
          "nothing else");
}

} // namespace

int main()
{
    ready_before_hello_is_inert();
    stale_generation_is_dropped();
    duplicate_hello_is_idempotent();
    operation_token_and_payload_kind_are_enforced();
    cancel_and_late_completion();
    older_generation_restart_is_rejected();
    terminal_and_shutdown_close_the_attempt();
    repeated_attempts_do_not_leak();

    if (failures != 0) {
        std::fprintf(stderr, "%d link handshake race checks failed\n", failures);
        return 1;
    }
    std::puts("link handshake race tests passed");
    return 0;
}
