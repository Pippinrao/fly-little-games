#include <flynes/flynes_session.h>

#include "../harness/deterministic_executor.hpp"
#include "view/session_view.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

struct Counts
{
    int retains = 0;
    int releases = 0;
};

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void retain_context(void* context)
{
    static_cast<Counts*>(context)->retains += 1;
}

void release_context(void* context)
{
    static_cast<Counts*>(context)->releases += 1;
}

fly_session_result_v2 read_clock(void*, fly_session_clock_sample_v2* out)
{
    if (!out) return FLY_SESSION_V2_INVALID_ARGUMENT;
    out->continuous_ns = 1;
    out->suspend_inclusive = 1;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 post_task(void*, fly_session_task_v2_t*)
{
    return FLY_SESSION_V2_ACCEPTED;
}

fly_session_result_v2 arm_timer(void*, std::uint64_t, const std::uint8_t*,
                                std::uint64_t, fly_session_task_v2_t*)
{
    return FLY_SESSION_V2_ACCEPTED;
}

fly_session_result_v2 cancel_timer(void*, std::uint64_t)
{
    return FLY_SESSION_V2_ACCEPTED;
}

fly_session_result_v2 watch_platform(void*,
                                     const fly_session_op_token_v2*,
                                     fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_ACCEPTED;
}

fly_session_result_v2 stop_platform(void*, const fly_session_op_token_v2*)
{
    return FLY_SESSION_V2_OK;
}

fly_session_config_v2 make_config()
{
    fly_session_config_v2 config{};
    config.struct_size = FLY_SESSION_CONFIG_V2_SIZE;
    config.abi_version = FLY_SESSION_ABI_VERSION_2;
    config.action_queue_capacity = 2;
    config.notice_queue_capacity = 2;
    return config;
}

fly_session_clock_port_v2 make_clock(Counts* counts)
{
    fly_session_clock_port_v2 port{};
    port.struct_size = FLY_SESSION_CLOCK_PORT_V2_SIZE;
    port.abi_version = FLY_SESSION_ABI_VERSION_2;
    port.context = counts;
    port.retain = retain_context;
    port.release = release_context;
    port.read_continuous = read_clock;
    return port;
}

fly_session_executor_port_v2 make_executor(Counts* counts)
{
    fly_session_executor_port_v2 port{};
    port.struct_size = FLY_SESSION_EXECUTOR_PORT_V2_SIZE;
    port.abi_version = FLY_SESSION_ABI_VERSION_2;
    port.context = counts;
    port.retain = retain_context;
    port.release = release_context;
    port.post = post_task;
    port.arm_timer = arm_timer;
    port.cancel_timer = cancel_timer;
    return port;
}

fly_session_platform_state_port_v2 make_platform(Counts* counts)
{
    fly_session_platform_state_port_v2 port{};
    port.struct_size = FLY_SESSION_PLATFORM_STATE_PORT_V2_SIZE;
    port.abi_version = FLY_SESSION_ABI_VERSION_2;
    port.context = counts;
    port.retain = retain_context;
    port.release = release_context;
    port.watch = watch_platform;
    port.stop = stop_platform;
    return port;
}

void test_create_validates_before_retaining()
{
    Counts clock_counts;
    Counts executor_counts;
    Counts platform_counts;
    auto clock = make_clock(&clock_counts);
    auto executor = make_executor(&executor_counts);
    auto platform = make_platform(&platform_counts);
    fly_session_ports_v2 ports{};
    ports.struct_size = FLY_SESSION_PORTS_V2_SIZE;
    ports.abi_version = FLY_SESSION_ABI_VERSION_2;
    ports.clock = &clock;
    ports.executor = &executor;
    ports.platform_state = &platform;

    auto config = make_config();
    fly_session_v2_t* engine = reinterpret_cast<fly_session_v2_t*>(1);

    auto bad_config = config;
    bad_config.struct_size -= 1;
    check(fly_session_create_v2(&bad_config, &ports, &engine) ==
              FLY_SESSION_V2_ABI_MISMATCH,
          "undersized config rejected");
    check(engine == nullptr, "failed create clears output");
    check(clock_counts.retains == 0 && executor_counts.retains == 0 &&
              platform_counts.retains == 0,
          "invalid config retains no providers");

    auto bad_ports = ports;
    bad_ports.reserved_zero = 1;
    check(fly_session_create_v2(&config, &bad_ports, &engine) ==
              FLY_SESSION_V2_INVALID_ARGUMENT,
          "nonzero reserved port field rejected");
    check(clock_counts.retains == 0 && executor_counts.retains == 0 &&
              platform_counts.retains == 0,
          "invalid port table retains no providers");

    auto missing_clock = ports;
    missing_clock.clock = nullptr;
    check(fly_session_create_v2(&config, &missing_clock, &engine) ==
              FLY_SESSION_V2_UNSUPPORTED,
          "missing required clock rejected");

    auto incomplete_executor = executor;
    incomplete_executor.post = nullptr;
    auto missing_function = ports;
    missing_function.executor = &incomplete_executor;
    check(fly_session_create_v2(&config, &missing_function, &engine) ==
              FLY_SESSION_V2_UNSUPPORTED,
          "missing required executor function rejected");

    fly_session_key_port_v2 incomplete_key{};
    incomplete_key.struct_size = FLY_SESSION_KEY_PORT_V2_SIZE;
    incomplete_key.abi_version = FLY_SESSION_ABI_VERSION_2;
    incomplete_key.context = &clock_counts;
    incomplete_key.retain = retain_context;
    incomplete_key.release = release_context;
    auto missing_key_function = ports;
    missing_key_function.key = &incomplete_key;
    check(fly_session_create_v2(&config, &missing_key_function, &engine) ==
              FLY_SESSION_V2_UNSUPPORTED,
          "present key provider must implement the complete contract");
    check(clock_counts.retains == 0,
          "invalid optional provider is rejected before any retain");

    check(fly_session_create_v2(&config, &ports, &engine) == FLY_SESSION_V2_OK,
          "optional camera may be absent");
    check(engine != nullptr, "valid create returns engine");
    check(clock_counts.retains == 1 && executor_counts.retains == 1 &&
              platform_counts.retains == 1,
          "valid create retains each provider once");
    check(fly_session_begin_shutdown_v2(engine, 1) == FLY_SESSION_V2_ACCEPTED,
          "fresh engine shutdown accepted");
    check(fly_session_destroy_v2(engine) == FLY_SESSION_V2_OK,
          "shutdown-complete fresh engine destroys");
    check(clock_counts.releases == 1 && executor_counts.releases == 1 &&
              platform_counts.releases == 1,
          "destroy releases each provider once");

    auto legacy_ports = ports;
    legacy_ports.struct_size = FLY_SESSION_PORTS_V2_R0_SIZE;
    engine = nullptr;
    check(fly_session_create_v2(&config, &legacy_ports, &engine) == FLY_SESSION_V2_OK,
          "R0 ports prefix remains source and binary compatible");
    check(fly_session_begin_shutdown_v2(engine, 2) == FLY_SESSION_V2_ACCEPTED &&
              fly_session_destroy_v2(engine) == FLY_SESSION_V2_OK,
          "R0 prefix engine shuts down normally");
}

struct StuckPlatform
{
    fly_session_op_token_v2 token{};
    fly_session_inbox_v2_t* inbox = nullptr;
    int retains = 0;
    int releases = 0;
    int stops = 0;

    ~StuckPlatform() { fly_session_inbox_release_v2(inbox); }

    fly_session_platform_state_port_v2 port()
    {
        fly_session_platform_state_port_v2 result{};
        result.struct_size = FLY_SESSION_PLATFORM_STATE_PORT_V2_SIZE;
        result.abi_version = FLY_SESSION_ABI_VERSION_2;
        result.context = this;
        result.retain = retain;
        result.release = release;
        result.watch = watch;
        result.stop = stop;
        return result;
    }

private:
    static void retain(void* context)
    {
        static_cast<StuckPlatform*>(context)->retains += 1;
    }

    static void release(void* context)
    {
        static_cast<StuckPlatform*>(context)->releases += 1;
    }

    static fly_session_result_v2 watch(void* context,
                                       const fly_session_op_token_v2* token,
                                       fly_session_inbox_v2_t* inbox)
    {
        auto* self = static_cast<StuckPlatform*>(context);
        self->token = *token;
        self->inbox = inbox;
        fly_session_inbox_retain_v2(inbox);
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 stop(void* context,
                                      const fly_session_op_token_v2*)
    {
        static_cast<StuckPlatform*>(context)->stops += 1;
        return FLY_SESSION_V2_ACCEPTED;
    }
};

void test_fail_closed_shutdown()
{
    Counts clock_counts;
    auto clock = make_clock(&clock_counts);
    DeterministicExecutor executor_context;
    auto executor = executor_context.port();
    StuckPlatform platform_context;
    auto platform = platform_context.port();
    fly_session_ports_v2 ports{};
    ports.struct_size = FLY_SESSION_PORTS_V2_SIZE;
    ports.abi_version = FLY_SESSION_ABI_VERSION_2;
    ports.clock = &clock;
    ports.executor = &executor;
    ports.platform_state = &platform;
    auto config = make_config();

    fly_session_v2_t* engine = nullptr;
    check(fly_session_create_v2(&config, &ports, &engine) == FLY_SESSION_V2_OK,
          "stuck-provider engine creates");
    check(fly_session_begin_shutdown_v2(engine, 801) == FLY_SESSION_V2_ACCEPTED &&
              platform_context.stops == 1,
          "shutdown requests provider stop once");
    check(fly_session_destroy_v2(engine) == FLY_SESSION_V2_BUSY,
          "destroy refuses an outstanding provider operation");

    fly_session_port_event_v2 terminal{};
    terminal.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    terminal.abi_version = FLY_SESSION_ABI_VERSION_2;
    terminal.token = platform_context.token;
    terminal.event_sequence = 1;
    terminal.event_kind = FLY_SESSION_PORT_EVENT_PLATFORM_STATE_V2;
    terminal.terminal = 1;
    terminal.result = FLY_SESSION_V2_CANCELLED;
    check(fly_session_deliver_v2(platform_context.inbox, &terminal) ==
              FLY_SESSION_V2_ACCEPTED,
          "shutdown still accepts the outstanding terminal cleanup");
    check(fly_session_destroy_v2(engine) == FLY_SESSION_V2_BUSY,
          "queued terminal must be consumed before destroy");
    executor_context.run_all();
    check(fly_session_deliver_v2(platform_context.inbox, &terminal) ==
              FLY_SESSION_V2_CLOSED,
          "completed shutdown inbox safely rejects late callbacks");
    check(fly_session_destroy_v2(engine) == FLY_SESSION_V2_OK,
          "destroy succeeds after terminal cleanup");
    check(platform_context.retains == 1 && platform_context.releases == 1,
          "stuck provider context releases after terminal cleanup");
}

void test_view_and_token_outlive_engine()
{
    Counts clock_counts;
    Counts executor_counts;
    Counts platform_counts;
    auto clock = make_clock(&clock_counts);
    auto executor = make_executor(&executor_counts);
    auto platform = make_platform(&platform_counts);
    fly_session_ports_v2 ports{};
    ports.struct_size = FLY_SESSION_PORTS_V2_SIZE;
    ports.abi_version = FLY_SESSION_ABI_VERSION_2;
    ports.clock = &clock;
    ports.executor = &executor;
    ports.platform_state = &platform;
    auto config = make_config();

    fly_session_v2_t* first_engine = nullptr;
    check(fly_session_create_v2(&config, &ports, &first_engine) == FLY_SESSION_V2_OK,
          "first view engine creates");

    fly_session_view_v2_t* old_view = nullptr;
    check(fly_session_acquire_view_v2(first_engine, &old_view) == FLY_SESSION_V2_OK,
          "initial immutable view acquired");
    fly_session_snapshot_v2 old_snapshot{};
    old_snapshot.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE;
    old_snapshot.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_view_read_v2(old_view, &old_snapshot) == FLY_SESSION_V2_OK,
          "initial immutable view reads");
    check(old_snapshot.view_revision == 1, "initial view revision is one");
    check(old_snapshot.engine_state == FLY_SESSION_ENGINE_LOADING_V2,
          "initial view reports loading");
    check(old_snapshot.action_count == 1, "loading view exposes one R0 action");

    fly_session_action_descriptor_v2 descriptor{};
    std::uint32_t written = 0;
    check(fly_session_view_copy_actions_v2(old_view, 0, &descriptor, 1, &written) ==
              FLY_SESSION_V2_OK,
          "view action page copies");
    check(written == 1 && descriptor.approval_token != nullptr,
          "view action carries approval token");
    written = 99;
    check(fly_session_view_copy_actions_v2(old_view, 1, &descriptor, 1, &written) ==
              FLY_SESSION_V2_OK &&
              written == 0,
          "pagination end returns an empty page");
    check(fly_session_view_copy_actions_v2(old_view, 2, &descriptor, 1, &written) ==
              FLY_SESSION_V2_INVALID_ARGUMENT,
          "pagination rejects offsets beyond the fixed view");
    fly_session_candidate_v2 candidate{};
    fly_session_friend_v2 friend_item{};
    fly_session_game_choice_v2 game_choice{};
    check(old_snapshot.candidate_count == 0 && old_snapshot.friend_count == 0 &&
              old_snapshot.game_choice_count == 0 &&
              fly_session_view_copy_candidates_v2(
                  old_view, 0, &candidate, 1, &written) == FLY_SESSION_V2_OK &&
              written == 0 &&
              fly_session_view_copy_friends_v2(
                  old_view, 0, &friend_item, 1, &written) == FLY_SESSION_V2_OK &&
              written == 0 &&
              fly_session_view_copy_game_choices_v2(
                  old_view, 0, &game_choice, 1, &written) == FLY_SESSION_V2_OK &&
              written == 0,
          "empty candidate friend and game-choice pages are immutable and public");
    check(fly_session_view_copy_candidates_v2(
              old_view, 1, &candidate, 1, &written) ==
              FLY_SESSION_V2_INVALID_ARGUMENT,
          "candidate page rejects offsets beyond the fixed view");
    fly_session_approval_token_retain_v2(descriptor.approval_token);

    check(fly_session_begin_shutdown_v2(first_engine, 71) ==
              FLY_SESSION_V2_ACCEPTED,
          "shutdown request accepted");
    fly_session_view_v2_t* shutdown_view = nullptr;
    check(fly_session_acquire_view_v2(first_engine, &shutdown_view) ==
              FLY_SESSION_V2_OK,
          "shutdown view acquired");
    fly_session_snapshot_v2 shutdown_snapshot{};
    shutdown_snapshot.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE;
    shutdown_snapshot.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_view_read_v2(shutdown_view, &shutdown_snapshot) ==
              FLY_SESSION_V2_OK,
          "shutdown view reads");
    check(shutdown_snapshot.view_revision > old_snapshot.view_revision,
          "shutdown publishes a new revision");
    check(shutdown_snapshot.engine_state == FLY_SESSION_ENGINE_SHUTDOWN_COMPLETE_V2,
          "idle shutdown completes");
    fly_session_view_release_v2(shutdown_view);

    fly_session_action_v2 stale_action{};
    stale_action.struct_size = FLY_SESSION_ACTION_V2_SIZE;
    stale_action.abi_version = FLY_SESSION_ABI_VERSION_2;
    stale_action.request_id = 72;
    stale_action.expected_view_revision = old_snapshot.view_revision;
    stale_action.approval_token = descriptor.approval_token;
    check(fly_session_submit_action_v2(first_engine, &stale_action) ==
              FLY_SESSION_V2_STALE,
          "generation change revokes old token");

    check(fly_session_destroy_v2(first_engine) == FLY_SESSION_V2_OK,
          "shutdown-complete engine destroys");
    fly_session_snapshot_v2 reread{};
    reread.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE;
    reread.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_view_read_v2(old_view, &reread) == FLY_SESSION_V2_OK &&
              reread.view_revision == old_snapshot.view_revision,
          "old view remains readable after engine destroy");
    fly_session_view_release_v2(old_view);

    fly_session_v2_t* second_engine = nullptr;
    check(fly_session_create_v2(&config, &ports, &second_engine) == FLY_SESSION_V2_OK,
          "second engine creates");
    check(fly_session_submit_action_v2(second_engine, &stale_action) ==
              FLY_SESSION_V2_STALE,
          "approval token cannot cross engine instances");
    check(fly_session_begin_shutdown_v2(second_engine, 73) ==
              FLY_SESSION_V2_ACCEPTED,
          "second engine shutdown accepted");
    check(fly_session_destroy_v2(second_engine) == FLY_SESSION_V2_OK,
          "second engine destroys");

    fly_session_approval_token_release_v2(descriptor.approval_token);
    check(clock_counts.retains == 2 && clock_counts.releases == 2 &&
              executor_counts.retains == 2 && executor_counts.releases == 2 &&
              platform_counts.retains == 2 && platform_counts.releases == 2,
          "views and tokens do not retain platform providers");
}

void test_bounded_action_and_notice_queues()
{
    Counts clock_counts;
    Counts platform_counts;
    auto clock = make_clock(&clock_counts);
    DeterministicExecutor deterministic_executor;
    auto executor = deterministic_executor.port();
    auto platform = make_platform(&platform_counts);
    fly_session_ports_v2 ports{};
    ports.struct_size = FLY_SESSION_PORTS_V2_SIZE;
    ports.abi_version = FLY_SESSION_ABI_VERSION_2;
    ports.clock = &clock;
    ports.executor = &executor;
    ports.platform_state = &platform;
    auto config = make_config();

    fly_session_v2_t* engine = nullptr;
    check(fly_session_create_v2(&config, &ports, &engine) == FLY_SESSION_V2_OK,
          "queue test engine creates");
    fly_session_view_v2_t* view = nullptr;
    check(fly_session_acquire_view_v2(engine, &view) == FLY_SESSION_V2_OK,
          "queue test view acquired");
    fly_session_action_descriptor_v2 descriptor{};
    std::uint32_t written = 0;
    check(fly_session_view_copy_actions_v2(view, 0, &descriptor, 1, &written) ==
              FLY_SESSION_V2_OK &&
              written == 1,
          "queue test action copied");

    fly_session_action_v2 action{};
    action.struct_size = FLY_SESSION_ACTION_V2_SIZE;
    action.abi_version = FLY_SESSION_ABI_VERSION_2;
    action.expected_view_revision = 1;
    action.approval_token = descriptor.approval_token;
    action.request_id = 101;
    check(fly_session_submit_action_v2(engine, &action) == FLY_SESSION_V2_ACCEPTED,
          "first action accepted");
    check(fly_session_submit_action_v2(engine, &action) == FLY_SESSION_V2_DUPLICATE,
          "identical request id is idempotent");
    action.request_id = 102;
    check(fly_session_submit_action_v2(engine, &action) == FLY_SESSION_V2_ACCEPTED,
          "second action accepted");
    action.request_id = 103;
    check(fly_session_submit_action_v2(engine, &action) ==
              FLY_SESSION_V2_BACKPRESSURE,
          "third action rejected without a result slot");
    check(deterministic_executor.size() == 1,
          "one serialized worker drains accepted actions");

    deterministic_executor.run_all();
    fly_session_notice_v2 notice{};
    notice.struct_size = FLY_SESSION_NOTICE_V2_SIZE;
    notice.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_read_notice_v2(engine, &notice) == FLY_SESSION_V2_OK &&
              notice.request_id == 101 &&
              notice.kind == FLY_SESSION_NOTICE_ACTION_RESULT_V2 &&
              notice.outcome == FLY_SESSION_ACTION_APPLIED_V2,
          "first action result is ordered");
    notice.struct_size = FLY_SESSION_NOTICE_V2_SIZE;
    notice.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_read_notice_v2(engine, &notice) == FLY_SESSION_V2_OK &&
              notice.request_id == 102,
          "second action result is ordered");
    notice.struct_size = FLY_SESSION_NOTICE_V2_SIZE;
    notice.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_read_notice_v2(engine, &notice) == FLY_SESSION_V2_EMPTY,
          "notice queue drains to empty");

    action.request_id = 103;
    check(fly_session_submit_action_v2(engine, &action) == FLY_SESSION_V2_ACCEPTED,
          "backpressured action was not consumed");
    deterministic_executor.run_all();
    notice.struct_size = FLY_SESSION_NOTICE_V2_SIZE;
    notice.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_read_notice_v2(engine, &notice) == FLY_SESSION_V2_OK &&
              notice.request_id == 103,
          "retried action receives one result");

    fly_session_view_release_v2(view);
    check(fly_session_begin_shutdown_v2(engine, 104) == FLY_SESSION_V2_ACCEPTED,
          "queue test shutdown accepted");
    check(fly_session_destroy_v2(engine) == FLY_SESSION_V2_OK,
          "queue test engine destroys");
    check(deterministic_executor.retains() == 1 &&
              deterministic_executor.releases() == 1,
          "executor context lifetime is balanced");
}

void test_input_contract_rejects_invalid_or_inactive_input()
{
    Counts clock_counts;
    Counts executor_counts;
    Counts platform_counts;
    auto clock = make_clock(&clock_counts);
    auto executor = make_executor(&executor_counts);
    auto platform = make_platform(&platform_counts);
    fly_session_ports_v2 ports{};
    ports.struct_size = FLY_SESSION_PORTS_V2_SIZE;
    ports.abi_version = FLY_SESSION_ABI_VERSION_2;
    ports.clock = &clock;
    ports.executor = &executor;
    ports.platform_state = &platform;
    auto config = make_config();

    fly_session_v2_t* engine = nullptr;
    check(fly_session_create_v2(&config, &ports, &engine) == FLY_SESSION_V2_OK,
          "input contract engine creates");
    fly_session_input_v2 input{};
    input.struct_size = FLY_SESSION_INPUT_V2_SIZE;
    input.abi_version = FLY_SESSION_ABI_VERSION_2;
    input.scope.struct_size = FLY_SESSION_SCOPE_V2_SIZE;
    input.scope.abi_version = FLY_SESSION_ABI_VERSION_2;
    input.scope.kind = FLY_SESSION_SCOPE_GAME_V2;
    input.scope.link_id[0] = 1;
    input.scope.branch_id[0] = 1;
    input.expected_seat_revision = 1;
    input.local_device_input_id = 1;
    input.capture_clock.struct_size = FLY_SESSION_CLOCK_SAMPLE_V2_SIZE;
    input.capture_clock.abi_version = FLY_SESSION_ABI_VERSION_2;
    input.capture_clock.suspend_inclusive = 1;
    input.capture_clock.boot_generation[0] = 1;

    /*
     * The DUAL four-port mask is a tail append, so the required prefix is the
     * pre-DUAL size. `--struct_size` from the *new* full size is therefore a
     * legal (if mask-less) input, and the boundary that must still be refused is
     * one byte below the pre-DUAL prefix.
     */
    auto below_prefix = input;
    below_prefix.struct_size = FLY_SESSION_INPUT_V2_R0_SIZE - 1;
    check(fly_session_submit_input_v2(engine, &below_prefix) ==
              FLY_SESSION_V2_ABI_MISMATCH,
          "an input shorter than the pre-DUAL prefix is rejected synchronously");
    auto pre_dual_input = input;
    pre_dual_input.struct_size = FLY_SESSION_INPUT_V2_R0_SIZE;
    check(fly_session_submit_input_v2(engine, &pre_dual_input) !=
              FLY_SESSION_V2_ABI_MISMATCH,
          "a pre-DUAL input prefix stays a legal input after the tail append");
    auto oversized_mask = input;
    oversized_mask.port_mask[2] = 0x100u;
    check(fly_session_submit_input_v2(engine, &oversized_mask) ==
              FLY_SESSION_V2_INVALID_ARGUMENT,
          "a DUAL port mask wider than one byte is rejected");
    auto reserved = input;
    reserved.reserved_zero = 1;
    check(fly_session_submit_input_v2(engine, &reserved) ==
              FLY_SESSION_V2_INVALID_ARGUMENT,
          "input reserved bits are rejected synchronously");
    auto invalid_scope = input;
    invalid_scope.scope.branch_id[0] = 0;
    check(fly_session_submit_input_v2(engine, &invalid_scope) ==
              FLY_SESSION_V2_INVALID_ARGUMENT,
          "input requires a complete game scope");
    check(fly_session_submit_input_v2(engine, &input) ==
              FLY_SESSION_V2_INVALID_STATE,
          "well-formed input is not accepted before a real running game");

    check(fly_session_begin_shutdown_v2(engine, 1201) ==
              FLY_SESSION_V2_ACCEPTED &&
              fly_session_destroy_v2(engine) == FLY_SESSION_V2_OK,
          "input contract engine shuts down");
}

/*
 * Step 1 of the DUAL integration: the public ABI grew by tail appends only, so
 * every older caller stays legal and nothing may be written past the size it
 * declared.
 */
void test_dual_prefix_appends_keep_older_callers_legal()
{
    check(FLY_SESSION_INPUT_V2_R0_SIZE < FLY_SESSION_INPUT_V2_SIZE &&
              FLY_SESSION_SNAPSHOT_V2_R0_SIZE < FLY_SESSION_SNAPSHOT_V2_SIZE &&
              FLY_SESSION_PORTS_V2_R1_SIZE < FLY_SESSION_PORTS_V2_SIZE &&
              FLY_SESSION_GAME_CHOICE_V2_R0_SIZE < FLY_SESSION_GAME_CHOICE_V2_SIZE,
          "every DUAL append grew its structure");

    Counts clock_counts;
    Counts executor_counts;
    Counts platform_counts;
    auto clock = make_clock(&clock_counts);
    auto executor = make_executor(&executor_counts);
    auto platform = make_platform(&platform_counts);
    fly_session_ports_v2 ports{};
    /* Exactly the pre-DUAL table: no dual_runtime slot at all. */
    ports.struct_size = FLY_SESSION_PORTS_V2_R1_SIZE;
    ports.abi_version = FLY_SESSION_ABI_VERSION_2;
    ports.clock = &clock;
    ports.executor = &executor;
    ports.platform_state = &platform;
    auto config = make_config();

    fly_session_v2_t* engine = nullptr;
    check(fly_session_create_v2(&config, &ports, &engine) == FLY_SESSION_V2_OK,
          "a pre-DUAL provider table still creates an engine");

    fly_session_view_v2_t* view = nullptr;
    check(fly_session_acquire_view_v2(engine, &view) == FLY_SESSION_V2_OK,
          "a pre-DUAL engine publishes a view");

    struct Guarded
    {
        fly_session_snapshot_v2 snapshot;
        std::uint64_t canary;
    };
    Guarded guarded{};
    guarded.canary = 0xA5A5A5A5A5A5A5A5ull;
    guarded.snapshot.struct_size = FLY_SESSION_SNAPSHOT_V2_R0_SIZE;
    guarded.snapshot.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_view_read_v2(view, &guarded.snapshot) == FLY_SESSION_V2_OK,
          "an older snapshot prefix reads successfully");
    check(guarded.canary == 0xA5A5A5A5A5A5A5A5ull,
          "the snapshot read never writes past the size the caller declared");
    check(guarded.snapshot.dual_mode == 0 && guarded.snapshot.dual_state == 0 &&
              guarded.snapshot.dual_freeze_reason == 0 &&
              guarded.snapshot.dual_frame_index == 0,
          "a pre-DUAL reader sees none of the appended DUAL block");

    fly_session_snapshot_v2 full{};
    full.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE;
    full.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_view_read_v2(view, &full) == FLY_SESSION_V2_OK,
          "a current snapshot reads successfully");
    check(full.dual_mode == FLY_SESSION_DUAL_MODE_NONE_V2 &&
              full.dual_state == FLY_SESSION_DUAL_UNLOADED_V2 &&
              full.dual_freeze_reason == FLY_SESSION_DUAL_FREEZE_NONE_V2,
          "a non-DUAL engine reports the neutral DUAL run state");

    /*
     * A reader from before the DUAL digest block: it declares exactly the R1
     * prefix, so it must be served the run state and none of the digest bytes
     * may be written into its buffer.
     */
    Guarded pre_digest{};
    pre_digest.canary = 0x5A5A5A5A5A5A5A5Aull;
    pre_digest.snapshot.struct_size = FLY_SESSION_SNAPSHOT_V2_R1_SIZE;
    pre_digest.snapshot.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_view_read_v2(view, &pre_digest.snapshot) ==
              FLY_SESSION_V2_OK,
          "a pre-digest snapshot prefix reads successfully");
    check(pre_digest.canary == 0x5A5A5A5A5A5A5A5Aull &&
              pre_digest.snapshot.dual_state_digest[0] == 0 &&
              pre_digest.snapshot.dual_pcm_digest[31] == 0,
          "the digest block is not written past the size the caller declared");

    /*
     * A reader from before the pending-config confirm block: it declares exactly
     * the R2 prefix, so it must be served the digest block and none of the
     * pending-config bytes may be written into its buffer.
     */
    Guarded pre_pending{};
    pre_pending.canary = 0xC3C3C3C3C3C3C3C3ull;
    pre_pending.snapshot.struct_size = FLY_SESSION_SNAPSHOT_V2_R2_SIZE;
    pre_pending.snapshot.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_view_read_v2(view, &pre_pending.snapshot) ==
              FLY_SESSION_V2_OK,
          "a pre-pending-config snapshot prefix reads successfully");
    check(pre_pending.canary == 0xC3C3C3C3C3C3C3C3ull &&
              pre_pending.snapshot.pending_config_id[0] == 0 &&
              pre_pending.snapshot.pending_config_local_confirmed == 0 &&
              pre_pending.snapshot.pending_config_peer_confirmed == 0,
          "the pending-config block is not written past the size the caller declared");

    check(full.pending_config_id[0] == 0 &&
              full.pending_config_local_confirmed == 0 &&
              full.pending_config_peer_confirmed == 0,
          "empty pending_config_id is unset config, not a platform bool");

    fly_session_view_release_v2(view);
    check(fly_session_begin_shutdown_v2(engine, 1301) == FLY_SESSION_V2_ACCEPTED &&
              fly_session_destroy_v2(engine) == FLY_SESSION_V2_OK,
          "pre-DUAL engine shuts down");
}

fly_session_result_v2 content_query(void*, const fly_session_op_token_v2*,
                                   std::uint32_t, fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_EMPTY;
}

fly_session_result_v2 content_cancel(void*, const fly_session_op_token_v2*)
{
    return FLY_SESSION_V2_OK;
}

/*
 * The content reference port is optional and tail-appended: a table from before
 * the slot must still create an engine, and a content table that cannot answer a
 * query must be refused rather than silently ignored (a platform supplying half a
 * port would otherwise look exactly like "no content is available", which is a
 * different and much more confusing failure).
 */
void test_content_port_is_optional_and_validated()
{
    Counts clock_counts;
    Counts executor_counts;
    Counts platform_counts;
    auto clock = make_clock(&clock_counts);
    auto executor = make_executor(&executor_counts);
    auto platform = make_platform(&platform_counts);
    auto config = make_config();

    fly_session_ports_v2 ports{};
    ports.abi_version = FLY_SESSION_ABI_VERSION_2;
    ports.clock = &clock;
    ports.executor = &executor;
    ports.platform_state = &platform;

    /* Exactly the pre-content table: no content slot at all. */
    ports.struct_size = FLY_SESSION_PORTS_V2_R2_SIZE;
    ports.content = nullptr;
    fly_session_v2_t* pre_content = nullptr;
    check(fly_session_create_v2(&config, &ports, &pre_content) ==
              FLY_SESSION_V2_OK,
          "a pre-content provider table still creates an engine");
    check(fly_session_begin_shutdown_v2(pre_content, 1401) ==
              FLY_SESSION_V2_ACCEPTED &&
              fly_session_destroy_v2(pre_content) == FLY_SESSION_V2_OK,
          "the pre-content engine shuts down");

    /* A content port that cannot answer a query is refused, not ignored. */
    fly_session_content_port_v2 broken{};
    broken.struct_size = FLY_SESSION_CONTENT_PORT_V2_SIZE;
    broken.abi_version = FLY_SESSION_ABI_VERSION_2;
    broken.context = &platform_counts;
    broken.retain = retain_context;
    broken.release = release_context;
    broken.query = nullptr;
    broken.cancel = content_cancel;
    ports.struct_size = FLY_SESSION_PORTS_V2_SIZE;
    ports.content = &broken;
    fly_session_v2_t* refused = nullptr;
    check(fly_session_create_v2(&config, &ports, &refused) ==
              FLY_SESSION_V2_UNSUPPORTED,
          "a content port without a query is refused");

    /* A complete read-only content port is accepted. */
    auto content = broken;
    content.query = content_query;
    ports.content = &content;
    fly_session_v2_t* engine = nullptr;
    check(fly_session_create_v2(&config, &ports, &engine) == FLY_SESSION_V2_OK,
          "a read-only content port is accepted");
    check(fly_session_begin_shutdown_v2(engine, 1402) ==
              FLY_SESSION_V2_ACCEPTED &&
              fly_session_destroy_v2(engine) == FLY_SESSION_V2_OK,
          "the content engine shuts down");
}

fly_session_game_choice_v2 make_catalog_choice(std::uint8_t tag)
{
    fly_session_game_choice_v2 item{};
    item.struct_size = FLY_SESSION_GAME_CHOICE_V2_SIZE;
    item.abi_version = FLY_SESSION_ABI_VERSION_2;
    item.content_id[0] = tag;
    item.source_choice_ref[0] = static_cast<std::uint8_t>(tag + 16);
    item.catalog_revision = 1000u + tag;
    item.progress_revision = 2000u + tag;
    item.selectable = 1;
    item.display_name_size = 1;
    item.display_name[0] = static_cast<std::uint8_t>('A' + tag);
    item.reason_key[0] = 'r';
    item.core_id[0] = static_cast<std::uint8_t>(0xC0u + tag);
    item.profile_id[0] = static_cast<std::uint8_t>(0xD0u + tag);
    item.options_id[0] = static_cast<std::uint8_t>(0xE0u + tag);
    return item;
}

void fill_populated_catalog_view(fly_session_view_v2_handle& view)
{
    view.game_choices.clear();
    view.game_choices.push_back(make_catalog_choice(1));
    view.game_choices.push_back(make_catalog_choice(2));
    view.snapshot.game_choice_count = 2;
}

bool old_layout_canary_intact(const std::uint8_t* storage, std::uint32_t count)
{
    const auto prefix = FLY_SESSION_GAME_CHOICE_V2_R0_SIZE;
    const auto tail = FLY_SESSION_GAME_CHOICE_V2_SIZE - prefix;
    for (std::uint32_t i = 0; i < count * tail; ++i)
    {
        if (storage[count * prefix + i] != 0xA5u)
            return false;
    }
    return true;
}

void fill_old_layout_page(std::vector<std::uint8_t>& storage, std::uint32_t count)
{
    const auto prefix = FLY_SESSION_GAME_CHOICE_V2_R0_SIZE;
    const auto tail = FLY_SESSION_GAME_CHOICE_V2_SIZE - prefix;
    storage.assign(count * prefix + count * tail, 0xA5u);
    auto* header = reinterpret_cast<fly_session_game_choice_v2*>(storage.data());
    header->struct_size = prefix;
    header->abi_version = FLY_SESSION_ABI_VERSION_2;
}

void test_game_choice_copy_keeps_old_element_stride()
{
    fly_session_view_v2_handle view{};
    fill_populated_catalog_view(view);
    const auto prefix = FLY_SESSION_GAME_CHOICE_V2_R0_SIZE;
    const auto full = FLY_SESSION_GAME_CHOICE_V2_SIZE;

    std::vector<std::uint8_t> one;
    fill_old_layout_page(one, 1);
    std::uint32_t written = 99;
    check(fly_session_view_copy_game_choices_v2(
              &view, 0,
              reinterpret_cast<fly_session_game_choice_v2*>(one.data()), 1,
              &written) == FLY_SESSION_V2_OK &&
              written == 1,
          "an R0 game-choice page copies one element");
    check(one[offsetof(fly_session_game_choice_v2, content_id)] == 1,
          "the R0 page receives the first element's prefix");
    check(old_layout_canary_intact(one.data(), 1),
          "the R0 game-choice copy never writes past the caller element");

    std::vector<std::uint8_t> two;
    fill_old_layout_page(two, 2);
    written = 99;
    check(fly_session_view_copy_game_choices_v2(
              &view, 0,
              reinterpret_cast<fly_session_game_choice_v2*>(two.data()), 2,
              &written) == FLY_SESSION_V2_OK &&
              written == 2,
          "an R0 game-choice page copies two elements");
    check(two[offsetof(fly_session_game_choice_v2, content_id)] == 1 &&
              two[prefix + offsetof(fly_session_game_choice_v2, content_id)] == 2,
          "the second R0 element begins at the old 272-byte stride");
    check(old_layout_canary_intact(two.data(), 2),
          "a two-element R0 page is not written at the new 368-byte stride");

    std::vector<std::uint8_t> page;
    fill_old_layout_page(page, 1);
    written = 99;
    check(fly_session_view_copy_game_choices_v2(
              &view, 1,
              reinterpret_cast<fly_session_game_choice_v2*>(page.data()), 1,
              &written) == FLY_SESSION_V2_OK &&
              written == 1 &&
              page[offsetof(fly_session_game_choice_v2, content_id)] == 2 &&
              old_layout_canary_intact(page.data(), 1),
          "pagination serves the requested R0 element without overrunning");

    fly_session_game_choice_v2 current[2]{};
    current[0].struct_size = full;
    current[0].abi_version = FLY_SESSION_ABI_VERSION_2;
    current[1].struct_size = full;
    current[1].abi_version = FLY_SESSION_ABI_VERSION_2;
    written = 0;
    check(fly_session_view_copy_game_choices_v2(&view, 0, current, 2, &written) ==
              FLY_SESSION_V2_OK &&
              written == 2 &&
              current[0].content_id[0] == 1 && current[0].core_id[0] == 0xC1u &&
              current[0].profile_id[0] == 0xD1u &&
              current[0].options_id[0] == 0xE1u &&
              current[1].content_id[0] == 2 && current[1].core_id[0] == 0xC2u,
          "a current game-choice page receives the bound core profile and options");

    std::vector<std::uint8_t> bad_size(full + prefix, 0xA5u);
    auto* bad_header = reinterpret_cast<fly_session_game_choice_v2*>(bad_size.data());
    bad_header->struct_size = prefix - 1;
    bad_header->abi_version = FLY_SESSION_ABI_VERSION_2;
    written = 99;
    check(fly_session_view_copy_game_choices_v2(
              &view, 0, bad_header, 1, &written) == FLY_SESSION_V2_ABI_MISMATCH &&
              written == 0,
          "a too-small game-choice element is rejected");
    check(bad_size[offsetof(fly_session_game_choice_v2, content_id)] == 0xA5u &&
              bad_size[prefix] == 0xA5u,
          "a size mismatch leaves the caller buffer untouched");

    std::vector<std::uint8_t> bad_abi(full + prefix, 0xA5u);
    auto* abi_header = reinterpret_cast<fly_session_game_choice_v2*>(bad_abi.data());
    abi_header->struct_size = prefix;
    abi_header->abi_version = 1;
    written = 99;
    check(fly_session_view_copy_game_choices_v2(
              &view, 0, abi_header, 1, &written) == FLY_SESSION_V2_ABI_MISMATCH &&
              written == 0 &&
              bad_abi[offsetof(fly_session_game_choice_v2, content_id)] == 0xA5u,
          "a version mismatch leaves the caller buffer untouched");

    std::vector<std::uint8_t> zeroed;
    fill_old_layout_page(zeroed, 2);
    auto* zero_header =
        reinterpret_cast<fly_session_game_choice_v2*>(zeroed.data());
    zero_header->struct_size = 0;
    zero_header->abi_version = 0;
    written = 99;
    check(fly_session_view_copy_game_choices_v2(
              &view, 0, zero_header, 2, &written) == FLY_SESSION_V2_OK &&
              written == 2 &&
              zeroed[offsetof(fly_session_game_choice_v2, content_id)] == 1 &&
              zeroed[prefix + offsetof(fly_session_game_choice_v2, content_id)] ==
                  2 &&
              old_layout_canary_intact(zeroed.data(), 2),
          "a zeroed old caller buffer still copies at the R0 272-byte stride");
}

} // namespace

int main()
{
    test_create_validates_before_retaining();
    test_view_and_token_outlive_engine();
    test_bounded_action_and_notice_queues();
    test_input_contract_rejects_invalid_or_inactive_input();
    test_dual_prefix_appends_keep_older_callers_legal();
    test_game_choice_copy_keeps_old_element_stride();
    test_content_port_is_optional_and_validated();
    test_fail_closed_shutdown();
    if (failures != 0)
    {
        std::fprintf(stderr, "flynes_session_v2_contract: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("flynes_session_v2_contract: PASS");
    return 0;
}
