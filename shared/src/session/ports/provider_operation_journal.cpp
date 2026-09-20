#include "provider_operation_journal.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace {

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

bool valid_token(const fly_session_op_token_v2& value) noexcept
{
    return value.struct_size == FLY_SESSION_OP_TOKEN_V2_SIZE &&
           value.abi_version == FLY_SESSION_ABI_VERSION_2 &&
           value.scope.struct_size == FLY_SESSION_SCOPE_V2_SIZE &&
           value.scope.abi_version == FLY_SESSION_ABI_VERSION_2 &&
           value.scope.reserved_zero == 0 && value.operation_id != 0 &&
           value.scope.kind >= FLY_SESSION_SCOPE_ENGINE_V2 &&
           value.scope.kind <= FLY_SESSION_SCOPE_GAME_V2;
}

bool same_event(const fly_session_port_event_v2& left,
                const fly_session_port_event_v2& right) noexcept
{
    return same_token(left.token, right.token) &&
           left.struct_size == right.struct_size &&
           left.abi_version == right.abi_version &&
           left.event_sequence == right.event_sequence &&
           left.event_kind == right.event_kind && left.terminal == right.terminal &&
           left.result == right.result && left.payload_kind == right.payload_kind &&
           left.payload_size == right.payload_size &&
           left.reserved_zero == right.reserved_zero &&
           std::memcmp(left.payload, right.payload, left.payload_size) == 0;
}

} // namespace

namespace flynes::session {

ProviderOperationJournal::ProviderOperationJournal(std::size_t active_capacity)
    : active_capacity_(active_capacity)
{
    if (active_capacity == 0)
        throw std::invalid_argument("provider journal capacity must be nonzero");
    records_.reserve(active_capacity * 2);
}

fly_session_result_v2 ProviderOperationJournal::expect(
    const fly_session_op_token_v2& operation_token,
    std::uint32_t payload_kind)
{
    if (!valid_token(operation_token) || payload_kind == 0)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    const auto existing = std::find_if(
        records_.begin(), records_.end(), [&operation_token](const Record& item) {
            return item.token.operation_id == operation_token.operation_id;
        });
    if (existing != records_.end())
    {
        return same_token(existing->token, operation_token) &&
                       existing->payload_kind == payload_kind
                   ? FLY_SESSION_V2_DUPLICATE
                   : FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    if (operation_token.operation_id <= highest_operation_id_)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    const auto active = static_cast<std::size_t>(std::count_if(
        records_.begin(), records_.end(), [](const Record& item) {
            return item.state == State::Pending;
        }));
    if (active >= active_capacity_)
        return FLY_SESSION_V2_BACKPRESSURE;
    records_.push_back(Record{operation_token, payload_kind, State::Pending, {}});
    highest_operation_id_ = operation_token.operation_id;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ProviderOperationJournal::accept(
    const fly_session_port_event_v2& event,
    ProviderOperationCompletion& completion)
{
    const auto record = std::find_if(
        records_.begin(), records_.end(), [&event](const Record& item) {
            return item.token.operation_id == event.token.operation_id;
        });
    if (record == records_.end() || !same_token(record->token, event.token) ||
        record->state == State::Cancelled)
        return FLY_SESSION_V2_STALE;
    if (record->state == State::Terminal)
        return same_event(record->terminal_event, event)
                   ? FLY_SESSION_V2_DUPLICATE
                   : FLY_SESSION_V2_CONTRACT_VIOLATION;

    ParsedProviderEvent parsed;
    const auto parse_result = parse_provider_event_v2(
        event, record->token, record->payload_kind, parsed);
    if (parse_result != FLY_SESSION_V2_OK)
        return parse_result;
    if (event.terminal == 0)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;

    record->state = State::Terminal;
    record->terminal_event = event;
    completion.payload_kind = event.payload_kind;
    completion.result = event.result;
    completion.terminal = true;
    completion.payload = std::move(parsed);
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ProviderOperationJournal::cancel(
    const fly_session_op_token_v2& operation_token) noexcept
{
    const auto record = std::find_if(
        records_.begin(), records_.end(), [&operation_token](const Record& item) {
            return item.token.operation_id == operation_token.operation_id;
        });
    if (record == records_.end() || !same_token(record->token, operation_token))
        return FLY_SESSION_V2_STALE;
    if (record->state != State::Pending)
        return FLY_SESSION_V2_DUPLICATE;
    record->state = State::Cancelled;
    return FLY_SESSION_V2_OK;
}

bool ProviderOperationJournal::has_pending() const noexcept
{
    return std::any_of(records_.begin(), records_.end(), [](const Record& item) {
        return item.state == State::Pending;
    });
}

} // namespace flynes::session
