#include <flynes/flynes_session.h>

#include "buffer_handle.hpp"
#include "ports/provider_events.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

fly_session_op_token_v2 token()
{
    fly_session_op_token_v2 value{};
    value.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    value.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.engine_instance_id[0] = 7;
    value.scope.struct_size = FLY_SESSION_SCOPE_V2_SIZE;
    value.scope.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.scope.kind = FLY_SESSION_SCOPE_LINK_V2;
    value.scope.link_id[0] = 9;
    value.connection_generation = 11;
    value.operation_id = 13;
    value.transition_id[0] = 15;
    return value;
}

template <typename Payload>
fly_session_port_event_v2 event_with(std::uint32_t kind, std::uint32_t terminal,
                                     const Payload& payload)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token();
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = terminal;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_provider_resource_event_v2 resource_payload()
{
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = 17;
    payload.generation = 19;
    payload.value0 = 1;
    payload.value1 = 23;
    return payload;
}

fly_session_provider_buffer_event_v2 buffer_payload(
    fly_session_buffer_v2_t* buffer)
{
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = buffer;
    payload.generation = 19;
    payload.logical_size = 3;
    return payload;
}

fly_session_provider_hash_event_v2 hash_payload(fly_session_buffer_v2_t* buffer)
{
    fly_session_provider_hash_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_HASH_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = 17;
    payload.buffer = buffer;
    payload.hash[0] = 1;
    return payload;
}

fly_session_provider_metrics_event_v2 metrics_payload()
{
    fly_session_provider_metrics_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_METRICS_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.values[0] = 1200;
    return payload;
}

fly_session_provider_store_event_v2 store_payload(
    fly_session_buffer_v2_t* buffer)
{
    fly_session_provider_store_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_STORE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = buffer;
    payload.revision = 29;
    payload.logical_size = 3;
    return payload;
}

fly_session_provider_end_event_v2 end_payload()
{
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    return payload;
}

void test_every_payload_kind_and_buffer_lifetime()
{
    using flynes::session::ParsedProviderEvent;
    using flynes::session::parse_provider_event_v2;

    const std::array<std::uint32_t, 10> resource_kinds{{
        FLY_SESSION_PROVIDER_DISCOVERY_CANDIDATE_V2,
        FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2,
        FLY_SESSION_PROVIDER_DISCOVERY_MTU_V2,
        FLY_SESSION_PROVIDER_BEARER_PATH_V2,
        FLY_SESSION_PROVIDER_KEY_AGREEMENT_V2,
        FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2,
        FLY_SESSION_PROVIDER_QUIC_CONNECTION_V2,
        FLY_SESSION_PROVIDER_QUIC_STREAM_V2,
        FLY_SESSION_PROVIDER_QUIC_PAYLOAD_BUDGET_V2,
        FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2}};
    for (const auto kind : resource_kinds)
    {
        const auto event = event_with(
            kind, kind == FLY_SESSION_PROVIDER_DISCOVERY_CANDIDATE_V2 ||
                          kind == FLY_SESSION_PROVIDER_DISCOVERY_MTU_V2
                      ? 0U
                      : 1U,
            resource_payload());
        ParsedProviderEvent parsed;
        check(parse_provider_event_v2(event, token(), kind, parsed) ==
                  FLY_SESSION_V2_OK,
              "resource payload kind parses");
    }

    const std::array<std::uint32_t, 10> buffer_kinds{{
        FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2,
        FLY_SESSION_PROVIDER_BEARER_CAPABILITIES_V2,
        FLY_SESSION_PROVIDER_BEARER_ENDPOINT_V2,
        FLY_SESSION_PROVIDER_KEY_PUBLIC_V2,
        FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2,
        FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2,
        FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
        FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
        FLY_SESSION_PROVIDER_QUIC_EXPORTER_V2,
        FLY_SESSION_PROVIDER_QUIC_DATA_V2}};
    for (const auto kind : buffer_kinds)
    {
        std::atomic<std::uint32_t> destroyed{0};
        const std::array<std::uint8_t, 3> bytes{{1, 2, 3}};
        auto* buffer = flynes::session::make_buffer_v2(
            bytes.data(), bytes.size(), &destroyed);
        const auto event = event_with(
            kind, kind == FLY_SESSION_PROVIDER_QUIC_DATA_V2 ||
                          kind == FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2
                      ? 0U
                      : 1U,
            buffer_payload(buffer));
        {
            ParsedProviderEvent parsed;
            check(parse_provider_event_v2(event, token(), kind, parsed) ==
                      FLY_SESSION_V2_OK,
                  "buffer payload kind parses");
            check(buffer->references.load() == 2,
                  "accepted buffer is retained exactly once");
            fly_session_buffer_release_v2(buffer);
            check(destroyed.load() == 0, "parsed event owns accepted buffer");
        }
        check(destroyed.load() == 1, "parsed event releases buffer exactly once");
    }

    const std::array<std::uint32_t, 6> hash_kinds{{
        FLY_SESSION_PROVIDER_KEY_HANDLE_V2,
        FLY_SESSION_PROVIDER_TLS_MATERIAL_V2,
        FLY_SESSION_PROVIDER_BEARER_CREDENTIAL_V2,
        FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2,
        FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2,
        FLY_SESSION_PROVIDER_CONTENT_CHOICE_V2}};
    for (const auto kind : hash_kinds)
    {
        const std::array<std::uint8_t, 1> bytes{{4}};
        auto* buffer = flynes::session::make_buffer_v2(bytes.data(), bytes.size());
        const auto event = event_with(kind, 1, hash_payload(buffer));
        {
            ParsedProviderEvent parsed;
            check(parse_provider_event_v2(event, token(), kind, parsed) ==
                      FLY_SESSION_V2_OK,
                  "hash payload kind parses");
        }
        fly_session_buffer_release_v2(buffer);
    }

    {
        std::atomic<std::uint32_t> destroyed{0};
        const std::array<std::uint8_t, 3> bytes{{5, 6, 7}};
        auto* buffer = flynes::session::make_buffer_v2(
            bytes.data(), bytes.size(), &destroyed);
        const auto event = event_with(
            FLY_SESSION_PROVIDER_SECURE_STORE_RECORD_V2, 1,
            store_payload(buffer));
        {
            ParsedProviderEvent parsed;
            check(parse_provider_event_v2(
                      event, token(),
                      FLY_SESSION_PROVIDER_SECURE_STORE_RECORD_V2,
                      parsed) == FLY_SESSION_V2_OK &&
                      parsed.value0 == 29 && parsed.buffer == buffer,
                  "secure-store read returns exact bytes and revision together");
            fly_session_buffer_release_v2(buffer);
            check(destroyed.load() == 0,
                  "parsed secure-store record owns the immutable bytes");
        }
        check(destroyed.load() == 1,
              "parsed secure-store record releases bytes exactly once");
    }

    {
        const std::array<std::uint8_t, 1> bytes{{4}};
        auto* buffer = flynes::session::make_buffer_v2(bytes.data(), bytes.size());
        auto payload = hash_payload(buffer);
        std::fill(std::begin(payload.hash), std::end(payload.hash),
                  std::uint8_t{0});
        const auto event = event_with(
            FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2, 1, payload);
        ParsedProviderEvent parsed;
        check(parse_provider_event_v2(
                  event, token(), FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2,
                  parsed) == FLY_SESSION_V2_OK,
              "server-only QUIC listener handshake permits an absent peer pin");
        fly_session_buffer_release_v2(buffer);
    }

    for (const auto kind : {FLY_SESSION_PROVIDER_QUIC_STATS_V2})
    {
        const auto event = event_with(kind, 1, metrics_payload());
        ParsedProviderEvent parsed;
        check(parse_provider_event_v2(event, token(), kind, parsed) ==
                  FLY_SESSION_V2_OK,
              "metrics payload kind parses");
    }

    for (const auto kind : {FLY_SESSION_PROVIDER_DISCOVERY_END_V2,
                            FLY_SESSION_PROVIDER_BEARER_END_V2,
                            FLY_SESSION_PROVIDER_QUIC_END_V2,
                            FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2})
    {
        const auto event = event_with(kind, 1, end_payload());
        ParsedProviderEvent parsed;
        check(parse_provider_event_v2(event, token(), kind, parsed) ==
                  FLY_SESSION_V2_OK,
              "end payload kind parses");
    }
}

void test_typed_async_failure_uses_end_payload_without_fake_resource()
{
    auto event = event_with(FLY_SESSION_PROVIDER_KEY_HANDLE_V2, 1,
                            end_payload());
    event.result = FLY_SESSION_V2_AUTH_FAILED;
    flynes::session::ParsedProviderEvent parsed;
    check(flynes::session::parse_provider_event_v2(
              event, token(), FLY_SESSION_PROVIDER_KEY_HANDLE_V2, parsed) ==
              FLY_SESSION_V2_OK &&
              parsed.payload_kind == FLY_SESSION_PROVIDER_KEY_HANDLE_V2 &&
              parsed.resource == 0 && parsed.buffer == nullptr,
          "typed asynchronous failure carries END without a fake resource");

    event.terminal = 0;
    check(flynes::session::parse_provider_event_v2(
              event, token(), FLY_SESSION_PROVIDER_KEY_HANDLE_V2, parsed) ==
              FLY_SESSION_V2_CONTRACT_VIOLATION,
          "a nonterminal operation cannot report a typed failure");
}

void test_quic_read_failure_is_a_typed_terminal()
{
    using flynes::session::ParsedProviderEvent;
    using flynes::session::parse_provider_event_v2;
    for (const auto failure : {FLY_SESSION_V2_IO_FAILED, FLY_SESSION_V2_CANCELLED})
    {
        auto event = event_with(FLY_SESSION_PROVIDER_QUIC_DATA_V2, 1, end_payload());
        event.result = failure;
        ParsedProviderEvent parsed;
        check(parse_provider_event_v2(event, token(), event.payload_kind, parsed) ==
                  FLY_SESSION_V2_OK && parsed.buffer == nullptr && parsed.resource == 0,
              "QUIC read error terminates with typed END and no fake buffer");
        event.terminal = 0;
        check(parse_provider_event_v2(event, token(), event.payload_kind, parsed) ==
                  FLY_SESSION_V2_CONTRACT_VIOLATION,
              "QUIC read error must be terminal");
        event.terminal = 1;
        event.payload_size -= 1;
        check(parse_provider_event_v2(event, token(), event.payload_kind, parsed) ==
                  FLY_SESSION_V2_ABI_MISMATCH,
              "QUIC read error validates END size");
        event.payload_size += 1;
        event.token.connection_generation += 1;
        check(parse_provider_event_v2(event, token(), event.payload_kind, parsed) ==
                  FLY_SESSION_V2_STALE, "QUIC read error preserves token fence");
        event.token = token();
        event.payload_kind = FLY_SESSION_PROVIDER_QUIC_END_V2;
        check(parse_provider_event_v2(event, token(), FLY_SESSION_PROVIDER_QUIC_DATA_V2, parsed) ==
                  FLY_SESSION_V2_CONTRACT_VIOLATION,
              "QUIC END kind cannot substitute for DATA read failure");
    }
    auto success = event_with(FLY_SESSION_PROVIDER_QUIC_DATA_V2, 1, end_payload());
    ParsedProviderEvent parsed;
    check(parse_provider_event_v2(success, token(), success.payload_kind, parsed) ==
              FLY_SESSION_V2_CONTRACT_VIOLATION,
          "successful DATA still cannot carry terminal END");
    auto discovery = event_with(FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2, 1, end_payload());
    discovery.result = FLY_SESSION_V2_IO_FAILED;
    check(parse_provider_event_v2(discovery, token(), discovery.payload_kind, parsed) ==
              FLY_SESSION_V2_CONTRACT_VIOLATION,
          "discovery byte termination contract is not broadened");
}

void test_mutations_fail_closed_without_taking_buffer()
{
    std::atomic<std::uint32_t> destroyed{0};
    const std::array<std::uint8_t, 3> bytes{{1, 2, 3}};
    auto* buffer = flynes::session::make_buffer_v2(
        bytes.data(), bytes.size(), &destroyed);
    const auto base = event_with(FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2, 0,
                                 buffer_payload(buffer));

    const auto rejected = [&](fly_session_port_event_v2 event,
                              fly_session_result_v2 expected,
                              const char* message) {
        flynes::session::ParsedProviderEvent parsed;
        check(flynes::session::parse_provider_event_v2(
                  event, token(), FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2,
                  parsed) == expected,
              message);
        check(buffer->references.load() == 1,
              "rejected payload does not retain its buffer");
    };

    auto event = base;
    --event.struct_size;
    rejected(event, FLY_SESSION_V2_ABI_MISMATCH, "outer size mutation rejected");
    event = base;
    ++event.abi_version;
    rejected(event, FLY_SESSION_V2_ABI_MISMATCH, "outer version mutation rejected");
    event = base;
    event.reserved_zero = 1;
    rejected(event, FLY_SESSION_V2_INVALID_ARGUMENT, "outer reserved rejected");
    event = base;
    event.terminal = 1;
    rejected(event, FLY_SESSION_V2_CONTRACT_VIOLATION, "terminal mutation rejected");
    event = base;
    ++event.token.connection_generation;
    rejected(event, FLY_SESSION_V2_STALE, "generation mutation rejected");
    event = base;
    ++event.token.operation_id;
    rejected(event, FLY_SESSION_V2_STALE, "operation mutation rejected");
    event = base;
    event.token.scope.reserved_zero = 1;
    rejected(event, FLY_SESSION_V2_INVALID_ARGUMENT, "scope reserved rejected");
    event = base;
    event.token.scope.kind = 99;
    rejected(event, FLY_SESSION_V2_INVALID_ARGUMENT, "scope kind rejected");
    event = base;
    ++event.payload_size;
    rejected(event, FLY_SESSION_V2_ABI_MISMATCH, "payload size mutation rejected");
    event = base;
    auto payload = buffer_payload(buffer);
    payload.buffer = nullptr;
    std::memcpy(event.payload, &payload, sizeof(payload));
    rejected(event, FLY_SESSION_V2_INVALID_ARGUMENT, "null buffer rejected");
    event = base;
    payload = buffer_payload(buffer);
    payload.reserved_zero = 1;
    std::memcpy(event.payload, &payload, sizeof(payload));
    rejected(event, FLY_SESSION_V2_INVALID_ARGUMENT,
             "payload reserved bytes rejected");
    event = base;
    payload = buffer_payload(buffer);
    --payload.struct_size;
    std::memcpy(event.payload, &payload, sizeof(payload));
    rejected(event, FLY_SESSION_V2_ABI_MISMATCH,
             "payload struct size rejected");
    event = base;
    payload = buffer_payload(buffer);
    ++payload.abi_version;
    std::memcpy(event.payload, &payload, sizeof(payload));
    rejected(event, FLY_SESSION_V2_ABI_MISMATCH,
             "payload version rejected");
    event = base;
    {
        flynes::session::ParsedProviderEvent parsed;
        check(flynes::session::parse_provider_event_v2(
                  event, token(), FLY_SESSION_PROVIDER_KEY_PUBLIC_V2, parsed) ==
                  FLY_SESSION_V2_CONTRACT_VIOLATION,
              "unexpected payload kind rejected");
        check(buffer->references.load() == 1,
              "kind conflict does not retain its buffer");
    }

    event = event_with(FLY_SESSION_PROVIDER_DISCOVERY_CANDIDATE_V2, 0,
                       resource_payload());
    auto resource = resource_payload();
    resource.resource = 0;
    std::memcpy(event.payload, &resource, sizeof(resource));
    {
        flynes::session::ParsedProviderEvent parsed;
        check(flynes::session::parse_provider_event_v2(
                  event, token(), FLY_SESSION_PROVIDER_DISCOVERY_CANDIDATE_V2,
                  parsed) == FLY_SESSION_V2_INVALID_ARGUMENT,
              "zero resource handle rejected");
    }

    auto connection = resource_payload();
    connection.value0 = 0;
    event = event_with(FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2, 1,
                       connection);
    {
        flynes::session::ParsedProviderEvent parsed;
        check(flynes::session::parse_provider_event_v2(
                  event, token(),
                  FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2, parsed) ==
                  FLY_SESSION_V2_INVALID_ARGUMENT,
              "discovery connection requires an exact physical role");
    }
    connection = resource_payload();
    connection.value1 = 22;
    event = event_with(FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2, 1,
                       connection);
    {
        flynes::session::ParsedProviderEvent parsed;
        check(flynes::session::parse_provider_event_v2(
                  event, token(),
                  FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2, parsed) ==
                  FLY_SESSION_V2_INVALID_ARGUMENT,
              "discovery connection rejects an ATT MTU below 23");
    }

    fly_session_buffer_release_v2(buffer);
    check(destroyed.load() == 1, "caller still owns every rejected buffer");
}

} // namespace

int main()
{
    test_every_payload_kind_and_buffer_lifetime();
    test_typed_async_failure_uses_end_payload_without_fake_resource();
    test_quic_read_failure_is_a_typed_terminal();
    test_mutations_fail_closed_without_taking_buffer();
    return failures == 0 ? 0 : 1;
}
