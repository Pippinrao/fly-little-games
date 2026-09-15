#include <flynes/flynes_session.h>

#include "../harness/deterministic_executor.hpp"

#include <cstdint>
#include <cstdio>

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

struct ContextCounts
{
    int retains = 0;
    int releases = 0;
};

void retain_context(void* context)
{
    static_cast<ContextCounts*>(context)->retains += 1;
}

void release_context(void* context)
{
    static_cast<ContextCounts*>(context)->releases += 1;
}

fly_session_result_v2 read_clock(void*, fly_session_clock_sample_v2* out)
{
    if (!out) return FLY_SESSION_V2_INVALID_ARGUMENT;
    out->continuous_ns = 1;
    out->suspend_inclusive = 1;
    return FLY_SESSION_V2_OK;
}

struct CapturingPlatform
{
    int retains = 0;
    int releases = 0;
    fly_session_op_token_v2 token{};
    fly_session_inbox_v2_t* inbox = nullptr;

    ~CapturingPlatform()
    {
        fly_session_inbox_release_v2(inbox);
    }

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
        static_cast<CapturingPlatform*>(context)->retains += 1;
    }

    static void release(void* context)
    {
        static_cast<CapturingPlatform*>(context)->releases += 1;
    }

    static fly_session_result_v2 watch(void* context,
                                       const fly_session_op_token_v2* token,
                                       fly_session_inbox_v2_t* inbox)
    {
        auto* self = static_cast<CapturingPlatform*>(context);
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

void test_completion_fencing()
{
    ContextCounts clock_counts;
    fly_session_clock_port_v2 clock{};
    clock.struct_size = FLY_SESSION_CLOCK_PORT_V2_SIZE;
    clock.abi_version = FLY_SESSION_ABI_VERSION_2;
    clock.context = &clock_counts;
    clock.retain = retain_context;
    clock.release = release_context;
    clock.read_continuous = read_clock;

    DeterministicExecutor executor_context;
    auto executor = executor_context.port();
    CapturingPlatform platform_context;
    auto platform = platform_context.port();
    fly_session_ports_v2 ports{};
    ports.struct_size = FLY_SESSION_PORTS_V2_SIZE;
    ports.abi_version = FLY_SESSION_ABI_VERSION_2;
    ports.clock = &clock;
    ports.executor = &executor;
    ports.platform_state = &platform;

    fly_session_config_v2 config{};
    config.struct_size = FLY_SESSION_CONFIG_V2_SIZE;
    config.abi_version = FLY_SESSION_ABI_VERSION_2;
    config.action_queue_capacity = 4;
    config.notice_queue_capacity = 4;
    fly_session_v2_t* engine = nullptr;
    check(fly_session_create_v2(&config, &ports, &engine) == FLY_SESSION_V2_OK,
          "fencing engine creates");
    check(platform_context.inbox != nullptr &&
              platform_context.token.operation_id != 0,
          "provider receives retained inbox and issued operation token");

    fly_session_view_v2_t* initial_view = nullptr;
    fly_session_snapshot_v2 initial_snapshot{};
    initial_snapshot.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE;
    initial_snapshot.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_acquire_view_v2(engine, &initial_view) == FLY_SESSION_V2_OK &&
              fly_session_view_read_v2(initial_view, &initial_snapshot) ==
                  FLY_SESSION_V2_OK,
          "initial fencing view reads");

    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = platform_context.token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_PLATFORM_STATE_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = 1;
    event.payload_size = 1;
    event.payload[0] = 0x5a;

    check(fly_session_deliver_v2(platform_context.inbox, &event) ==
              FLY_SESSION_V2_ACCEPTED,
          "first terminal completion accepted for serialization");
    check(fly_session_deliver_v2(platform_context.inbox, &event) ==
              FLY_SESSION_V2_DUPLICATE,
          "identical queued completion is idempotent");
    auto conflicting = event;
    conflicting.payload[0] ^= 0xff;
    check(fly_session_deliver_v2(platform_context.inbox, &conflicting) ==
              FLY_SESSION_V2_CONTRACT_VIOLATION,
          "conflicting terminal completion is rejected");

    executor_context.run_all();
    check(fly_session_deliver_v2(platform_context.inbox, &event) ==
              FLY_SESSION_V2_DUPLICATE,
          "identical consumed completion is idempotent");
    auto stale = event;
    stale.token.operation_id += 1;
    stale.event_sequence = 2;
    check(fly_session_deliver_v2(platform_context.inbox, &stale) ==
              FLY_SESSION_V2_STALE,
          "unknown operation token is stale");
    auto stale_authority = event;
    stale_authority.token.authority_term = 1;
    stale_authority.event_sequence = 2;
    check(fly_session_deliver_v2(platform_context.inbox, &stale_authority) ==
              FLY_SESSION_V2_STALE,
          "authority-term fence mismatch is stale");
    auto stale_writer = event;
    stale_writer.token.writer_generation = 1;
    stale_writer.event_sequence = 2;
    check(fly_session_deliver_v2(platform_context.inbox, &stale_writer) ==
              FLY_SESSION_V2_STALE,
          "writer-generation fence mismatch is stale");

    fly_session_view_v2_t* after_view = nullptr;
    fly_session_snapshot_v2 after_snapshot{};
    after_snapshot.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE;
    after_snapshot.abi_version = FLY_SESSION_ABI_VERSION_2;
    check(fly_session_acquire_view_v2(engine, &after_view) == FLY_SESSION_V2_OK &&
              fly_session_view_read_v2(after_view, &after_snapshot) ==
                  FLY_SESSION_V2_OK &&
              after_snapshot.view_revision == initial_snapshot.view_revision,
          "duplicate stale and conflicting completions do not mutate view");
    fly_session_view_release_v2(after_view);
    fly_session_view_release_v2(initial_view);

    check(fly_session_begin_shutdown_v2(engine, 901) == FLY_SESSION_V2_ACCEPTED,
          "fencing engine shutdown accepted");
    check(fly_session_destroy_v2(engine) == FLY_SESSION_V2_OK,
          "fencing engine destroys");
}

} // namespace

int main()
{
    test_completion_fencing();
    if (failures != 0)
    {
        std::fprintf(stderr, "flynes_session_v2_fencing: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("flynes_session_v2_fencing: PASS");
    return 0;
}
