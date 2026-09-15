#include <flynes/flynes_session.h>

#include "../harness/deterministic_executor.hpp"

#include <cstdint>
#include <cstdio>

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

    auto undersized = input;
    --undersized.struct_size;
    check(fly_session_submit_input_v2(engine, &undersized) ==
              FLY_SESSION_V2_ABI_MISMATCH,
          "undersized input is rejected synchronously");
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

} // namespace

int main()
{
    test_create_validates_before_retaining();
    test_view_and_token_outlive_engine();
    test_bounded_action_and_notice_queues();
    test_input_contract_rejects_invalid_or_inactive_input();
    test_fail_closed_shutdown();
    if (failures != 0)
    {
        std::fprintf(stderr, "flynes_session_v2_contract: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("flynes_session_v2_contract: PASS");
    return 0;
}
