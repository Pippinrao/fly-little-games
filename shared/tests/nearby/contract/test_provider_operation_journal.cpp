#include "ports/provider_operation_journal.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>

using namespace flynes::session;

namespace {

int failures = 0;
void check(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

fly_session_op_token_v2 token(std::uint64_t operation)
{
    fly_session_op_token_v2 value{};
    value.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    value.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.engine_instance_id[0] = 1;
    value.scope.struct_size = FLY_SESSION_SCOPE_V2_SIZE;
    value.scope.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.scope.kind = FLY_SESSION_SCOPE_LINK_V2;
    value.scope.link_id[0] = 2;
    value.connection_generation = 7;
    value.operation_id = operation;
    return value;
}

fly_session_port_event_v2 completion(
    const fly_session_op_token_v2& operation, std::uint32_t kind,
    fly_session_result_v2 result = FLY_SESSION_V2_OK,
    std::uint64_t sequence = 1)
{
    fly_session_port_event_v2 value{};
    value.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    value.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.token = operation;
    value.event_sequence = sequence;
    value.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    value.terminal = 1;
    value.result = result;
    value.payload_kind = kind;
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.payload_size = sizeof(payload);
    std::memcpy(value.payload, &payload, sizeof(payload));
    return value;
}

void test_exact_fences_and_terminal_semantics()
{
    ProviderOperationJournal journal(8);
    check(journal.expect(token(10), FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2) ==
              FLY_SESSION_V2_OK,
          "first operation is registered");
    check(journal.expect(token(9), FLY_SESSION_PAIR_KEY_CONFIRM_VERIFIED_V2) ==
              FLY_SESSION_V2_INVALID_ARGUMENT,
          "operation ids are strictly monotonic");
    check(journal.expect(token(10), FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2) ==
              FLY_SESSION_V2_DUPLICATE,
          "same registration is idempotent");

    auto stale = completion(token(10), FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2);
    stale.token.connection_generation = 8;
    ProviderOperationCompletion out{};
    check(journal.accept(stale, out) == FLY_SESSION_V2_STALE,
          "wrong generation cannot complete an operation");
    stale = completion(token(10), FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2);
    stale.token.scope.link_id[0] = 3;
    check(journal.accept(stale, out) == FLY_SESSION_V2_STALE,
          "wrong link cannot complete an operation");
    stale = completion(token(10), FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2);
    stale.token.engine_instance_id[0] = 4;
    check(journal.accept(stale, out) == FLY_SESSION_V2_STALE,
          "wrong engine cannot complete an operation");

    auto wrong_kind = completion(token(10),
                                 FLY_SESSION_PAIR_KEY_CONFIRM_VERIFIED_V2);
    check(journal.accept(wrong_kind, out) == FLY_SESSION_V2_CONTRACT_VIOLATION,
          "wrong completion purpose is a provider contract violation");

    const auto good = completion(token(10),
                                 FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2);
    check(journal.accept(good, out) == FLY_SESSION_V2_OK && out.terminal &&
              out.result == FLY_SESSION_V2_OK &&
              out.payload_kind == FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2,
          "exact terminal is accepted once");
    check(journal.accept(good, out) == FLY_SESSION_V2_DUPLICATE,
          "byte-identical terminal is idempotent");
    auto conflict = good;
    conflict.result = FLY_SESSION_V2_AUTH_FAILED;
    check(journal.accept(conflict, out) == FLY_SESSION_V2_CONTRACT_VIOLATION,
          "same sequence with different result is a violation");
}

void test_cancel_and_failure_completion()
{
    ProviderOperationJournal journal(2);
    check(journal.expect(token(20), FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2) ==
              FLY_SESSION_V2_OK,
          "cancel case registers");
    check(journal.cancel(token(20)) == FLY_SESSION_V2_OK,
          "live operation cancels once");
    ProviderOperationCompletion out{};
    check(journal.accept(completion(
              token(20), FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2), out) ==
              FLY_SESSION_V2_STALE,
          "late success after cancel cannot advance state");

    check(journal.expect(token(21), FLY_SESSION_PAIR_KEY_CONFIRM_VERIFIED_V2) ==
              FLY_SESSION_V2_OK,
          "failure case registers");
    const auto failure = completion(token(21),
                                    FLY_SESSION_PAIR_KEY_CONFIRM_VERIFIED_V2,
                                    FLY_SESSION_V2_AUTH_FAILED);
    check(journal.accept(failure, out) == FLY_SESSION_V2_OK && out.terminal &&
              out.result == FLY_SESSION_V2_AUTH_FAILED,
          "authenticated provider failure is a valid terminal, not success");
    check(!journal.has_pending(), "all operations are terminal or cancelled");
}

} // namespace

int main()
{
    test_exact_fences_and_terminal_semantics();
    test_cancel_and_failure_completion();
    return failures == 0 ? 0 : 1;
}
