#include "provider_events.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace {

static_assert(sizeof(fly_session_provider_resource_event_v2) <= 64);
static_assert(sizeof(fly_session_provider_buffer_event_v2) <= 64);
static_assert(sizeof(fly_session_provider_store_event_v2) <= 64);
static_assert(sizeof(fly_session_provider_hash_event_v2) <= 64);
static_assert(sizeof(fly_session_provider_metrics_event_v2) <= 64);
static_assert(sizeof(fly_session_provider_end_event_v2) <= 64);

bool same_token(const fly_session_op_token_v2& left,
                const fly_session_op_token_v2& right) noexcept
{
    return left.struct_size == right.struct_size &&
           left.abi_version == right.abi_version &&
           std::memcmp(left.engine_instance_id, right.engine_instance_id,
                       sizeof(left.engine_instance_id)) == 0 &&
           left.scope.struct_size == right.scope.struct_size &&
           left.scope.abi_version == right.scope.abi_version &&
           left.scope.kind == right.scope.kind &&
           left.scope.reserved_zero == right.scope.reserved_zero &&
           std::memcmp(left.scope.link_id, right.scope.link_id,
                       sizeof(left.scope.link_id)) == 0 &&
           std::memcmp(left.scope.branch_id, right.scope.branch_id,
                       sizeof(left.scope.branch_id)) == 0 &&
           left.connection_generation == right.connection_generation &&
           left.config_revision == right.config_revision &&
           left.authority_term == right.authority_term &&
           left.writer_generation == right.writer_generation &&
           left.timeline_epoch == right.timeline_epoch &&
           left.seat_revision == right.seat_revision &&
           left.mode_generation == right.mode_generation &&
           left.media_generation == right.media_generation &&
           left.operation_id == right.operation_id &&
           std::memcmp(left.transition_id, right.transition_id,
                       sizeof(left.transition_id)) == 0;
}

fly_session_result_v2 validate_token(
    const fly_session_op_token_v2& value) noexcept
{
    if (value.struct_size != FLY_SESSION_OP_TOKEN_V2_SIZE ||
        value.abi_version != FLY_SESSION_ABI_VERSION_2 ||
        value.scope.struct_size != FLY_SESSION_SCOPE_V2_SIZE ||
        value.scope.abi_version != FLY_SESSION_ABI_VERSION_2)
        return FLY_SESSION_V2_ABI_MISMATCH;
    if (value.scope.reserved_zero != 0 || value.operation_id == 0 ||
        value.scope.kind < FLY_SESSION_SCOPE_ENGINE_V2 ||
        value.scope.kind > FLY_SESSION_SCOPE_GAME_V2)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    return FLY_SESSION_V2_OK;
}

enum class PayloadForm { Resource, Buffer, Store, Hash, Metrics, End, Unknown };

struct PayloadContract final
{
    PayloadForm form = PayloadForm::Unknown;
    std::uint32_t terminal = 0;
};

PayloadContract contract_for(std::uint32_t kind) noexcept
{
    switch (kind)
    {
    case FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2:
    case FLY_SESSION_PAIR_KEY_CONFIRM_VERIFIED_V2:
        return {PayloadForm::End, 1};
    case FLY_SESSION_PROVIDER_DISCOVERY_CANDIDATE_V2:
    case FLY_SESSION_PROVIDER_DISCOVERY_MTU_V2:
        return {PayloadForm::Resource, 0};
    case FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2:
    case FLY_SESSION_PROVIDER_BEARER_PATH_V2:
    case FLY_SESSION_PROVIDER_KEY_AGREEMENT_V2:
    case FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2:
    case FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2:
    case FLY_SESSION_PROVIDER_QUIC_CONNECTION_V2:
    case FLY_SESSION_PROVIDER_QUIC_STREAM_V2:
    case FLY_SESSION_PROVIDER_QUIC_PAYLOAD_BUDGET_V2:
        return {PayloadForm::Resource, 1};
    case FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2:
    case FLY_SESSION_PROVIDER_QUIC_DATA_V2:
        return {PayloadForm::Buffer, 0};
    case FLY_SESSION_PROVIDER_BEARER_CAPABILITIES_V2:
    case FLY_SESSION_PROVIDER_BEARER_ENDPOINT_V2:
    case FLY_SESSION_PROVIDER_KEY_PUBLIC_V2:
    case FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2:
    case FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2:
    case FLY_SESSION_PROVIDER_CRYPTO_MAC_V2:
    case FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2:
    case FLY_SESSION_PROVIDER_QUIC_EXPORTER_V2:
        return {PayloadForm::Buffer, 1};
    case FLY_SESSION_PROVIDER_SECURE_STORE_RECORD_V2:
        return {PayloadForm::Store, 1};
    case FLY_SESSION_PROVIDER_KEY_HANDLE_V2:
    case FLY_SESSION_PROVIDER_TLS_MATERIAL_V2:
    case FLY_SESSION_PROVIDER_BEARER_CREDENTIAL_V2:
    case FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2:
    case FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2:
    case FLY_SESSION_PROVIDER_CONTENT_CHOICE_V2:
        return {PayloadForm::Hash, 1};
    case FLY_SESSION_PROVIDER_QUIC_STATS_V2:
        return {PayloadForm::Metrics, 1};
    case FLY_SESSION_PROVIDER_DISCOVERY_END_V2:
    case FLY_SESSION_PROVIDER_BEARER_END_V2:
    case FLY_SESSION_PROVIDER_QUIC_END_V2:
    case FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2:
        return {PayloadForm::End, 1};
    default:
        return {};
    }
}

template <typename Payload>
fly_session_result_v2 read_payload(const fly_session_port_event_v2& source,
                                   Payload& payload) noexcept
{
    if (source.payload_size != sizeof(Payload))
        return FLY_SESSION_V2_ABI_MISMATCH;
    std::memcpy(&payload, source.payload, sizeof(payload));
    if (payload.struct_size != sizeof(Payload) ||
        payload.abi_version != FLY_SESSION_ABI_VERSION_2)
        return FLY_SESSION_V2_ABI_MISMATCH;
    if (payload.reserved_zero != 0 || payload.reserved_zero2 != 0)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    return FLY_SESSION_V2_OK;
}

bool all_zero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    for (std::size_t index = 0; index < size; ++index)
    {
        if (bytes[index] != 0)
            return false;
    }
    return true;
}

} // namespace

namespace flynes::session {

ParsedProviderEvent::~ParsedProviderEvent()
{
    release();
}

ParsedProviderEvent::ParsedProviderEvent(ParsedProviderEvent&& other) noexcept
    : payload_kind(other.payload_kind), resource(other.resource),
      generation(other.generation), value0(other.value0), value1(other.value1),
      hash(other.hash), buffer(std::exchange(other.buffer, nullptr))
{
}

ParsedProviderEvent& ParsedProviderEvent::operator=(
    ParsedProviderEvent&& other) noexcept
{
    if (this == &other)
        return *this;
    release();
    payload_kind = other.payload_kind;
    resource = other.resource;
    generation = other.generation;
    value0 = other.value0;
    value1 = other.value1;
    hash = other.hash;
    buffer = std::exchange(other.buffer, nullptr);
    return *this;
}

void ParsedProviderEvent::release() noexcept
{
    fly_session_buffer_release_v2(buffer);
    buffer = nullptr;
}

fly_session_result_v2 parse_provider_event_v2(
    const fly_session_port_event_v2& source,
    const fly_session_op_token_v2& expected_token,
    std::uint32_t expected_payload_kind,
    ParsedProviderEvent& destination) noexcept
{
    if (source.struct_size != FLY_SESSION_PORT_EVENT_V2_SIZE ||
        source.abi_version != FLY_SESSION_ABI_VERSION_2)
        return FLY_SESSION_V2_ABI_MISMATCH;
    if (source.event_sequence == 0 ||
        source.event_kind != FLY_SESSION_PORT_EVENT_OPERATION_V2 ||
        source.terminal > 1 || source.reserved_zero != 0 ||
        source.payload_size > sizeof(source.payload))
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    const auto token_result = validate_token(source.token);
    if (token_result != FLY_SESSION_V2_OK)
        return token_result;
    if (!same_token(source.token, expected_token))
        return FLY_SESSION_V2_STALE;
    if (source.payload_kind != expected_payload_kind)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;

    const auto contract = contract_for(source.payload_kind);
    if (contract.form == PayloadForm::Unknown)
        return FLY_SESSION_V2_UNSUPPORTED;
    if (source.result > FLY_SESSION_V2_OK)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;

    // A terminal provider operation, including a failed QUIC read, keeps its
    // expected typed payload kind but carries only the standard END record.
    // Successful QUIC DATA remains nonterminal. Requiring a fake key,
    // buffer, or path on failure would turn ordinary asynchronous errors into
    // contract violations and tempt adapters to manufacture capabilities.
    if (source.result != FLY_SESSION_V2_OK)
    {
        if (source.terminal != 1 ||
            (contract.terminal != 1 &&
             source.payload_kind != FLY_SESSION_PROVIDER_QUIC_DATA_V2))
            return FLY_SESSION_V2_CONTRACT_VIOLATION;
        fly_session_provider_end_event_v2 payload{};
        const auto result = read_payload(source, payload);
        if (result != FLY_SESSION_V2_OK)
            return result;
        ParsedProviderEvent parsed;
        parsed.payload_kind = source.payload_kind;
        destination = std::move(parsed);
        return FLY_SESSION_V2_OK;
    }

    if (source.terminal != contract.terminal)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;

    ParsedProviderEvent parsed;
    parsed.payload_kind = source.payload_kind;
    fly_session_result_v2 result = FLY_SESSION_V2_OK;
    if (contract.form == PayloadForm::Resource)
    {
        fly_session_provider_resource_event_v2 payload{};
        result = read_payload(source, payload);
        if (result == FLY_SESSION_V2_OK && payload.resource == 0)
            result = FLY_SESSION_V2_INVALID_ARGUMENT;
        if (result == FLY_SESSION_V2_OK &&
            (source.payload_kind == FLY_SESSION_PROVIDER_DISCOVERY_MTU_V2 ||
             source.payload_kind ==
                 FLY_SESSION_PROVIDER_QUIC_PAYLOAD_BUDGET_V2) &&
            payload.value0 == 0)
            result = FLY_SESSION_V2_INVALID_ARGUMENT;
        if (result == FLY_SESSION_V2_OK &&
            source.payload_kind ==
                FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2 &&
            ((payload.value0 != FLY_SESSION_DISCOVERY_PHYSICAL_CENTRAL_V2 &&
              payload.value0 !=
                  FLY_SESSION_DISCOVERY_PHYSICAL_PERIPHERAL_V2) ||
             payload.value1 < 23 || payload.value1 > 517 ||
             payload.generation == 0))
            result = FLY_SESSION_V2_INVALID_ARGUMENT;
        parsed.resource = payload.resource;
        parsed.generation = payload.generation;
        parsed.value0 = payload.value0;
        parsed.value1 = payload.value1;
    }
    else if (contract.form == PayloadForm::Buffer)
    {
        fly_session_provider_buffer_event_v2 payload{};
        result = read_payload(source, payload);
        std::uint64_t actual_size = 0;
        if (result == FLY_SESSION_V2_OK && payload.flags != 0)
            result = FLY_SESSION_V2_INVALID_ARGUMENT;
        if (result == FLY_SESSION_V2_OK &&
            fly_session_buffer_size_v2(payload.buffer, &actual_size) !=
                FLY_SESSION_V2_OK)
            result = FLY_SESSION_V2_INVALID_ARGUMENT;
        if (result == FLY_SESSION_V2_OK && payload.logical_size != actual_size)
            result = FLY_SESSION_V2_INVALID_ARGUMENT;
        parsed.generation = payload.generation;
        parsed.value0 = payload.logical_size;
        parsed.buffer = payload.buffer;
    }
    else if (contract.form == PayloadForm::Hash)
    {
        fly_session_provider_hash_event_v2 payload{};
        result = read_payload(source, payload);
        std::uint64_t ignored_size = 0;
        if (result == FLY_SESSION_V2_OK && payload.resource == 0)
            result = FLY_SESSION_V2_INVALID_ARGUMENT;
        if (result == FLY_SESSION_V2_OK &&
            fly_session_buffer_size_v2(payload.buffer, &ignored_size) !=
                FLY_SESSION_V2_OK)
            result = FLY_SESSION_V2_INVALID_ARGUMENT;
        if (result == FLY_SESSION_V2_OK &&
            source.payload_kind != FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2 &&
            all_zero(payload.hash, sizeof(payload.hash)))
            result = FLY_SESSION_V2_INVALID_ARGUMENT;
        parsed.resource = payload.resource;
        std::copy(std::begin(payload.hash), std::end(payload.hash),
                  parsed.hash.begin());
        parsed.buffer = payload.buffer;
    }
    else if (contract.form == PayloadForm::Store)
    {
        fly_session_provider_store_event_v2 payload{};
        result = read_payload(source, payload);
        std::uint64_t actual_size = 0;
        if (result == FLY_SESSION_V2_OK &&
            (payload.revision == 0 || payload.flags != 0))
            result = FLY_SESSION_V2_INVALID_ARGUMENT;
        if (result == FLY_SESSION_V2_OK &&
            fly_session_buffer_size_v2(payload.buffer, &actual_size) !=
                FLY_SESSION_V2_OK)
            result = FLY_SESSION_V2_INVALID_ARGUMENT;
        if (result == FLY_SESSION_V2_OK &&
            payload.logical_size != actual_size)
            result = FLY_SESSION_V2_INVALID_ARGUMENT;
        parsed.value0 = payload.revision;
        parsed.value1 = payload.logical_size;
        parsed.buffer = payload.buffer;
    }
    else if (contract.form == PayloadForm::Metrics)
    {
        fly_session_provider_metrics_event_v2 payload{};
        result = read_payload(source, payload);
        parsed.value0 = payload.values[0];
        parsed.value1 = payload.values[1];
    }
    else
    {
        fly_session_provider_end_event_v2 payload{};
        result = read_payload(source, payload);
    }

    if (result != FLY_SESSION_V2_OK)
    {
        parsed.buffer = nullptr;
        return result;
    }
    if (parsed.buffer)
        fly_session_buffer_retain_v2(parsed.buffer);
    destination = std::move(parsed);
    return FLY_SESSION_V2_OK;
}

} // namespace flynes::session
