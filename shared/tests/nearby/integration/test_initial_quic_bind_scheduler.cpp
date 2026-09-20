#include "link/initial_quic_bind_scheduler.hpp"
#include "nearby/harness/endpoint_offer_scheduler.hpp"
#include "wire/quic_contract.hpp"
#include "wire/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using namespace flynes::session;

int failures = 0;
void check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

std::array<std::uint8_t, 65> public_key(bool second)
{
    static constexpr std::array<std::uint8_t, 65> first = {{
        0x04, 0x6b, 0x17, 0xd1, 0xf2, 0xe1, 0x2c, 0x42,
        0x47, 0xf8, 0xbc, 0xe6, 0xe5, 0x63, 0xa4, 0x40,
        0xf2, 0x77, 0x03, 0x7d, 0x81, 0x2d, 0xeb, 0x33,
        0xa0, 0xf4, 0xa1, 0x39, 0x45, 0xd8, 0x98, 0xc2,
        0x96, 0x4f, 0xe3, 0x42, 0xe2, 0xfe, 0x1a, 0x7f,
        0x9b, 0x8e, 0xe7, 0xeb, 0x4a, 0x7c, 0x0f, 0x9e,
        0x16, 0x2b, 0xce, 0x33, 0x57, 0x6b, 0x31, 0x5e,
        0xce, 0xcb, 0xb6, 0x40, 0x68, 0x37, 0xbf, 0x51,
        0xf5
    }};
    static constexpr std::array<std::uint8_t, 65> second_point = {{
        0x04, 0x7c, 0xf2, 0x7b, 0x18, 0x8d, 0x03, 0x4f,
        0x7e, 0x8a, 0x52, 0x38, 0x03, 0x04, 0xb5, 0x1a,
        0xc3, 0xc0, 0x89, 0x69, 0xe2, 0x77, 0xf2, 0x1b,
        0x35, 0xa6, 0x0b, 0x48, 0xfc, 0x47, 0x66, 0x99,
        0x78, 0x07, 0x77, 0x55, 0x10, 0xdb, 0x8e, 0xd0,
        0x40, 0x29, 0x3d, 0x9a, 0xc6, 0x9f, 0x74, 0x30,
        0xdb, 0xba, 0x7d, 0xad, 0xe6, 0x3c, 0xe9, 0x82,
        0x29, 0x9e, 0x04, 0xb7, 0x9d, 0x22, 0x78, 0x73,
        0xd1
    }};
    return second ? second_point : first;
}

fly_session_port_event_v2 resource_event(
    const InitialQuicBindEffect& effect, std::uint64_t resource,
    std::uint64_t value0 = 0)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = effect.token;
    event.event_sequence = effect.token.operation_id;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = effect.expected_payload_kind;
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    payload.value0 = value0;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 end_event(const InitialQuicBindEffect& effect)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = effect.token;
    event.event_sequence = effect.token.operation_id;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = effect.expected_payload_kind;
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 buffer_event(
    const InitialQuicBindEffect& effect,
    const std::vector<std::uint8_t>& bytes,
    bool terminal,
    fly_session_buffer_v2_t** owned)
{
    const fly_session_bytes_v2 source{
        bytes.empty() ? nullptr : bytes.data(),
        static_cast<std::uint32_t>(bytes.size()), 0};
    check(fly_session_buffer_create_copy_v2(source, owned) == FLY_SESSION_V2_OK,
          "fixture creates immutable buffer");
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = effect.token;
    event.event_sequence = effect.token.operation_id;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = terminal ? 1u : 0u;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = effect.expected_payload_kind;
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = *owned;
    payload.logical_size = bytes.size();
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 handshake_event(
    const InitialQuicBindEffect& effect,
    const std::vector<std::uint8_t>& bytes,
    fly_session_buffer_v2_t** owned)
{
    const fly_session_bytes_v2 source{
        bytes.data(), static_cast<std::uint32_t>(bytes.size()), 0};
    check(fly_session_buffer_create_copy_v2(source, owned) == FLY_SESSION_V2_OK,
          "fixture creates handshake facts buffer");
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = effect.token;
    event.event_sequence = effect.token.operation_id;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = effect.expected_payload_kind;
    fly_session_provider_hash_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_HASH_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = 900;
    payload.buffer = *owned;
    const auto digest = wire::sha256(bytes.data(), bytes.size());
    std::copy(digest.begin(), digest.end(), payload.hash);
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

wire::BearerPlanBytes plan()
{
    wire::BearerPlanBytes value{};
    value[0] = 4;
    value[1] = 1;
    value[2] = 2;
    value[3] = 1;
    value[4] = 1;
    value[5] = 1;
    value[6] = 30;
    value[11] = 1;
    value[12] = 1;
    return value;
}

InitialQuicBindStartV1 start(wire::PairRoleV1 role, std::uint64_t first)
{
    InitialQuicBindStartV1 value{};
    value.engine_instance_id[0] = static_cast<std::uint8_t>(role);
    value.link_id[0] = 9;
    value.generation = 7;
    value.first_operation_id = first;
    value.local_role = role;
    value.transcript_hash[0] = 1;
    value.session_id[0] = 2;
    value.selected_plan = plan();
    value.listener_spki_hash[0] = 3;
    value.local_identity_public = public_key(role == wire::PairRoleV1::Responder);
    value.peer_identity_public = public_key(role == wire::PairRoleV1::Initiator);
    value.ecdh_secret = 77;
    value.bearer_path = 88;
    value.tls_material = role == wire::PairRoleV1::Responder ? 99 : 0;
    return value;
}

struct Pipe
{
    std::vector<std::uint8_t> bytes;
    bool finished = false;
};

bool drive_one(InitialQuicBindScheduler& scheduler,
               Pipe& incoming, Pipe& outgoing,
               bool listener, std::uint64_t& next_resource)
{
    const auto pending = scheduler.poll_effect();
    if (!pending) return false;
    const auto& effect = *pending;
    fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
    if (effect.kind == InitialQuicBindEffectKind::DeriveKey) {
        result = scheduler.complete(resource_event(effect, next_resource++));
    } else if (effect.kind == InitialQuicBindEffectKind::StartConnection) {
        check(effect.listener == listener && effect.policy.require_full_tls13 == 1 &&
                  effect.policy.forbid_resumption == 1 &&
                  effect.policy.forbid_zero_rtt == 1,
              "connection uses exact full-handshake policy and TLS role");
        result = scheduler.complete(resource_event(effect, next_resource++));
    } else if (effect.kind == InitialQuicBindEffectKind::InspectHandshake) {
        wire::QuicHandshakeFactsV2 facts{};
        facts.tls_major = 1;
        facts.tls_minor = 3;
        facts.full_handshake = true;
        facts.pin_verifier_invoked = !listener;
        facts.peer_certificate_verified = !listener;
        std::copy_n("flynes-nearby/2", 16, facts.alpn.begin());
        if (!listener) facts.der_spki_hash[0] = 3;
        std::array<std::uint8_t, wire::kQuicHandshakeFactsWireSizeV2> encoded{};
        check(wire::encode_quic_handshake_facts_v2(facts, &encoded) ==
                  wire::Status::Ok,
              "fixture encodes canonical handshake facts");
        std::vector<std::uint8_t> bytes(encoded.begin(), encoded.end());
        fly_session_buffer_v2_t* owned = nullptr;
        const auto event = handshake_event(effect, bytes, &owned);
        result = scheduler.complete(event);
        fly_session_buffer_release_v2(owned);
    } else if (effect.kind == InitialQuicBindEffectKind::Exporter ||
               effect.kind == InitialQuicBindEffectKind::Hmac) {
        std::vector<std::uint8_t> bytes;
        if (effect.kind == InitialQuicBindEffectKind::Exporter) {
            bytes.assign(32, 0);
            bytes[0] = 7;
            check(effect.exporter_context.size() == 32 && effect.info ==
                      std::vector<std::uint8_t>(
                          {'E','X','P','O','R','T','E','R','-','f','l','y','n','e','s','-',
                           'n','e','a','r','b','y','-','v','1'}),
                  "exporter uses exact label, context and output contract");
        } else {
            const auto digest = wire::sha256(effect.input.data(), effect.input.size());
            bytes.assign(digest.begin(), digest.end());
        }
        fly_session_buffer_v2_t* owned = nullptr;
        const auto event = buffer_event(effect, bytes, true, &owned);
        result = scheduler.complete(event);
        fly_session_buffer_release_v2(owned);
    } else if (effect.kind == InitialQuicBindEffectKind::OpenBindStream ||
               effect.kind == InitialQuicBindEffectKind::AcceptBindStream) {
        check(effect.accept == listener,
              "connector opens and listener accepts the unique bidi bind stream");
        /* Two fresh handles, evaluated left to right. Writing both increments
         * inside one call argument list was unsequenced and therefore
         * undefined: GCC rejects it, MSVC silently picks an order. */
        const auto first_resource = next_resource++;
        const auto second_resource = next_resource++;
        result = scheduler.complete(resource_event(
            effect, first_resource, second_resource));
    } else if (effect.kind == InitialQuicBindEffectKind::Write) {
        outgoing.bytes.insert(outgoing.bytes.end(), effect.input.begin(),
                              effect.input.end());
        if (effect.finish) outgoing.finished = true;
        result = scheduler.complete(end_event(effect));
    } else if (effect.kind == InitialQuicBindEffectKind::Read) {
        if (incoming.bytes.empty() && !incoming.finished) return false;
        std::vector<std::uint8_t> chunk;
        if (!incoming.bytes.empty()) {
            const auto size = (std::min<std::size_t>)(
                incoming.bytes.size(),
                (std::min<std::size_t>)(17, effect.read_credit));
            chunk.assign(incoming.bytes.begin(), incoming.bytes.begin() + size);
            incoming.bytes.erase(incoming.bytes.begin(), incoming.bytes.begin() + size);
        }
        fly_session_buffer_v2_t* owned = nullptr;
        const auto event = buffer_event(effect, chunk, false, &owned);
        result = scheduler.complete(event);
        fly_session_buffer_release_v2(owned);
    }
    check(result == FLY_SESSION_V2_OK, "scheduler accepts valid provider result");
    return true;
}

void exact_two_role_channel_bind()
{
    InitialPlanScheduler unused_plan_i;
    InitialPlanScheduler unused_plan_r;
    InitialBearerScheduler unused_bearer_i(unused_plan_i);
    InitialBearerScheduler unused_bearer_r(unused_plan_r);
    EndpointOfferScheduler endpoint_i(unused_bearer_i);
    EndpointOfferScheduler endpoint_r(unused_bearer_r);
    std::vector<std::uint8_t> endpoint(18, 0);
    endpoint[12] = 127;
    endpoint[15] = 1;
    endpoint[16] = 0xd6;
    endpoint[17] = 0xd8;
    EndpointOfferSchedulerTestFactory::seal_ready(endpoint_i, false, endpoint);
    EndpointOfferSchedulerTestFactory::seal_ready(endpoint_r, true, endpoint);

    InitialQuicBindScheduler connector(endpoint_i);
    InitialQuicBindScheduler listener(endpoint_r);
    check(connector.begin(start(wire::PairRoleV1::Initiator, 1000)) &&
              listener.begin(start(wire::PairRoleV1::Responder, 2000)),
          "both TLS roles begin only from endpoint-ready evidence");
    Pipe connector_to_listener;
    Pipe listener_to_connector;
    std::uint64_t connector_resource = 3000;
    std::uint64_t listener_resource = 4000;
    for (int guard = 0; guard < 500 &&
         (!connector.channel_bound() || !listener.channel_bound()); ++guard) {
        const bool a = drive_one(connector, listener_to_connector,
                                 connector_to_listener, false,
                                 connector_resource);
        const bool b = drive_one(listener, connector_to_listener,
                                 listener_to_connector, true,
                                 listener_resource);
        if (!a && !b) {
            check(false, "duplex bind flow made progress");
            break;
        }
    }
    check(connector.channel_bound() && listener.channel_bound(),
          "both sides reach CHANNEL_BOUND only after ACK and reciprocal FIN");
    check(connector.channel_id() == listener.channel_id(),
          "both sides bind the same exporter-derived channel id");
    check(connector_to_listener.bytes.empty() && listener_to_connector.bytes.empty() &&
              connector_to_listener.finished && listener_to_connector.finished,
          "bind stream consumes exact records and both FIN directions");
}

} // namespace

int main()
{
    exact_two_role_channel_bind();
    if (failures != 0) return 1;
    std::puts("initial QUIC ChannelBind scheduler tests passed");
    return 0;
}
