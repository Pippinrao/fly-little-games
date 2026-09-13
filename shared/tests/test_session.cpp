#include <flynes/flynes_session.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" int flynes_session_header_compiles_as_c(void);

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

fly_session_config make_config()
{
    fly_session_config config{};
    config.struct_size = FLY_SESSION_CONFIG_V1_SIZE;
    config.version = FLY_SESSION_CONFIG_VERSION_1;
    return config;
}

void test_create_tick_snapshot_and_empty_poll()
{
    check(flynes_session_header_compiles_as_c() > 0, "C header translation unit");

    fly_session_t* session = nullptr;
    fly_session_config config = make_config();
    check(fly_session_create(&config, &session) == FLY_RESULT_OK, "create");
    check(session != nullptr, "create handle");

    check(fly_session_tick(session, UINT64_C(1000)) == FLY_RESULT_OK, "tick");

    fly_session_snapshot snapshot{};
    snapshot.struct_size = FLY_SESSION_SNAPSHOT_V1_SIZE;
    snapshot.version = FLY_SESSION_SNAPSHOT_VERSION_1;
    check(fly_session_get_snapshot(session, &snapshot) == FLY_RESULT_OK, "snapshot");
    check(snapshot.ui_state == FLY_SESSION_UI_IDLE, "fresh handle with no pair attempt is idle");
    check(snapshot.committed_through.has_frame == 0u, "GENESIS has_frame");
    check(snapshot.committed_through.frame_index == 0u, "GENESIS index");
    check(snapshot.state_verified_through.kind == FLY_EVIDENCE_CURSOR_NONE, "NONE evidence");

    fly_session_command command{};
    command.struct_size = FLY_SESSION_COMMAND_V1_SIZE;
    command.version = FLY_SESSION_COMMAND_VERSION_1;
    check(fly_session_poll_command(session, &command) == FLY_RESULT_OK, "empty poll");
    check(command.kind == FLY_SESSION_COMMAND_NONE, "idle poll reports no command");
    check(command.command_id == 0u, "idle poll command_id empty");
    check(command.transition_id == 0u, "poll never fills transition_id");

    fly_session_event event{};
    event.struct_size = FLY_SESSION_EVENT_V1_SIZE;
    event.version = FLY_SESSION_EVENT_VERSION_1;
    event.kind = FLY_SESSION_EVENT_USER;
    check(fly_session_submit_event(session, &event) == FLY_RESULT_INVALID_STATE,
          "payload-less event kind fails closed");

    uint8_t byte = 0;
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, &byte, 1u) ==
              FLY_RESULT_INVALID_STATE,
          "raw stream bytes fail closed");
    check(fly_session_receive_datagram(session, FLY_SESSION_QUIC_INPUT, &byte, 1u) ==
              FLY_RESULT_INVALID_STATE,
          "raw datagram bytes fail closed");
    check(fly_session_receive_stream(session, 99u, &byte, 1u) == FLY_RESULT_INVALID_ARGUMENT,
          "bad channel");

    fly_session_command_result result{};
    result.struct_size = FLY_SESSION_COMMAND_RESULT_V1_SIZE;
    result.version = FLY_SESSION_COMMAND_RESULT_VERSION_1;
    check(fly_session_complete_command(session, &result) == FLY_RESULT_INVALID_STATE,
          "completion with no polled command fails closed");

    fly_session_destroy(session);
    fly_session_destroy(nullptr);

    fly_session_t* missing = reinterpret_cast<fly_session_t*>(1);
    check(fly_session_create(nullptr, &missing) == FLY_RESULT_INVALID_ARGUMENT, "null config");
    check(missing == nullptr, "failed create clears out");
}

} // namespace

static_assert(sizeof(fly_result) == sizeof(std::int32_t), "fly_result must stay 32-bit");
static_assert(FLY_SESSION_QUIC_CONTROL == 1, "QUIC Control mapping");
static_assert(FLY_SESSION_QUIC_INPUT == 2, "QUIC Input mapping");
static_assert(FLY_SESSION_QUIC_STATE_COMMIT == 3, "QUIC StateCommit mapping");
static_assert(FLY_SESSION_QUIC_BULK == 4, "QUIC Bulk mapping");
static_assert(FLY_SESSION_QUIC_ROM == 5, "QUIC ROM mapping");
static_assert(FLY_SESSION_QUIC_VIDEO == 6, "QUIC Video mapping");
static_assert(FLY_SESSION_QUIC_AUDIO == 7, "QUIC Audio mapping");

static_assert(offsetof(fly_frame_cursor_v1, struct_size) == 0, "cursor prefix");
static_assert(offsetof(fly_frame_cursor_v1, abi_version) == 4, "cursor version");
static_assert(offsetof(fly_frame_cursor_v1, has_frame) == 8, "cursor has_frame");
static_assert(offsetof(fly_frame_cursor_v1, reserved_zero) == 12, "cursor reserved");
static_assert(offsetof(fly_frame_cursor_v1, frame_index) == 16, "cursor index");
static_assert(FLY_FRAME_CURSOR_V1_SIZE == 24u, "cursor v1 size");
static_assert(FLY_FRAME_CURSOR_V1_SIZE == sizeof(fly_frame_cursor_v1), "cursor sizeof");

static_assert(offsetof(fly_evidence_cursor_v1, kind) == 8, "evidence kind");
static_assert(offsetof(fly_evidence_cursor_v1, timeline_epoch) == 16, "evidence epoch");
static_assert(offsetof(fly_evidence_cursor_v1, cursor) == 24, "evidence nested cursor");
static_assert(offsetof(fly_evidence_cursor_v1, evidence_hash) == 48, "evidence hash");
static_assert(FLY_EVIDENCE_CURSOR_V1_SIZE == 80u, "evidence v1 size");
static_assert(FLY_EVIDENCE_CURSOR_V1_SIZE == sizeof(fly_evidence_cursor_v1),
              "evidence sizeof");

static_assert(offsetof(fly_session_event, kind) == 8, "event kind");
static_assert(FLY_SESSION_EVENT_V1_SIZE == 16u, "event v1 size");
static_assert(offsetof(fly_session_command, command_id) == 8, "command id");
static_assert(offsetof(fly_session_command, kind) == 24, "command kind");
static_assert(FLY_SESSION_COMMAND_V1_SIZE == 32u, "command v1 size");
static_assert(offsetof(fly_session_snapshot, committed_through) == 24, "snapshot cursor");
static_assert(FLY_SESSION_SNAPSHOT_V1_SIZE == 128u, "snapshot v1 size");
static_assert(FLY_SESSION_SNAPSHOT_V1_SIZE == sizeof(fly_session_snapshot),
              "snapshot sizeof");

int main()
{
    test_create_tick_snapshot_and_empty_poll();
    if (failures != 0)
    {
        std::fprintf(stderr, "flynes_session_test: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("flynes_session_test: PASS");
    return 0;
}
