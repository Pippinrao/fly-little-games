#ifndef FLYNES_FLYNES_SESSION_H
#define FLYNES_FLYNES_SESSION_H

#include <flynes/flynes_app.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FLYNES_API
#define FLYNES_API
#endif

typedef struct fly_session_handle fly_session_t;

#define FLY_SESSION_CONFIG_VERSION_1 UINT32_C(1)
#define FLY_FRAME_CURSOR_VERSION_1 UINT32_C(1)
#define FLY_EVIDENCE_CURSOR_VERSION_1 UINT32_C(1)
#define FLY_SESSION_EVENT_VERSION_1 UINT32_C(1)
#define FLY_SESSION_COMMAND_VERSION_1 UINT32_C(1)
#define FLY_SESSION_COMMAND_RESULT_VERSION_1 UINT32_C(1)
#define FLY_SESSION_SNAPSHOT_VERSION_1 UINT32_C(1)

enum fly_session_quic_channel
{
    FLY_SESSION_QUIC_CONTROL = 1,
    FLY_SESSION_QUIC_INPUT = 2,
    FLY_SESSION_QUIC_STATE_COMMIT = 3,
    FLY_SESSION_QUIC_BULK = 4,
    FLY_SESSION_QUIC_ROM = 5,
    FLY_SESSION_QUIC_VIDEO = 6,
    FLY_SESSION_QUIC_AUDIO = 7
};

enum fly_session_event_kind
{
    FLY_SESSION_EVENT_USER = 1,
    FLY_SESSION_EVENT_LIFECYCLE = 2,
    FLY_SESSION_EVENT_CAPABILITY = 3,
    FLY_SESSION_EVENT_RUNTIME = 4,
    FLY_SESSION_EVENT_MEDIA = 5,
    FLY_SESSION_EVENT_STORAGE = 6,
    FLY_SESSION_EVENT_TRANSPORT = 7
};

enum fly_session_command_kind
{
    FLY_SESSION_COMMAND_NONE = 0,
    /*
     * 2026-09-13 invite-code amendment (additive v1 values). The invite route
     * issues these through poll; the executor effect is:
     *  - INVITE_CODE_LOOKUP: transmit INVITE_CODE_LOOKUP_REQUEST_V1 (kind
     *    0x0214) for the live joiner attempt named by the command generation.
     *  - INVITE_CODE_CANCEL: stop the live lookup connection.
     *  - INVITE_REGENERATE: switch advertisement and displayed code/QR to the
     *    new invitation generation. The old generation is already dead.
     */
    FLY_SESSION_COMMAND_INVITE_CODE_LOOKUP = 1,
    FLY_SESSION_COMMAND_INVITE_CODE_CANCEL = 2,
    FLY_SESSION_COMMAND_INVITE_REGENERATE = 3
};

/*
 * Snapshot UI state. FLY_SESSION_UI_UNSPECIFIED (0) means this v1 ABI cannot
 * represent the current state (for example a live, failed or otherwise terminal
 * initial-plan attempt); it never means idle. The real lifecycle states are the
 * approved design's §2 table and require a versioned ABI addition
 * (struct_size/abi_version) instead of overloading 0.
 */
enum fly_session_ui_state
{
    FLY_SESSION_UI_UNSPECIFIED = 0,
    FLY_SESSION_UI_IDLE = 1
};

enum fly_evidence_cursor_kind
{
    FLY_EVIDENCE_CURSOR_NONE = 0,
    FLY_EVIDENCE_CURSOR_PRESENT = 1
};

/*
 * borrowed only for fly_session_create. reserved fields must be zero.
 * No BLE, QR, Wi-Fi, or friend types.
 */
typedef struct fly_session_config
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t reserved;
    uint32_t reserved1;
} fly_session_config;

#define FLY_SESSION_CONFIG_V1_SIZE \
    ((uint32_t)(offsetof(fly_session_config, reserved1) + sizeof(uint32_t)))

/*
 * Process C ABI for FrameCursorV1. has_frame is 0 (GENESIS) or 1 (FRAME).
 * GENESIS requires frame_index == 0. This is not a wire DTO.
 */
typedef struct fly_frame_cursor_v1
{
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t has_frame;
    uint32_t reserved_zero;
    uint64_t frame_index;
} fly_frame_cursor_v1;

#define FLY_FRAME_CURSOR_V1_SIZE \
    ((uint32_t)(offsetof(fly_frame_cursor_v1, frame_index) + sizeof(uint64_t)))

typedef struct fly_evidence_cursor_v1
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t kind;
    uint32_t reserved_zero;
    uint64_t timeline_epoch;
    fly_frame_cursor_v1 cursor;
    uint8_t evidence_hash[32];
} fly_evidence_cursor_v1;

#define FLY_EVIDENCE_CURSOR_V1_SIZE \
    ((uint32_t)(offsetof(fly_evidence_cursor_v1, evidence_hash) + 32u))

typedef struct fly_session_event
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t kind;
    uint32_t reserved;
} fly_session_event;

#define FLY_SESSION_EVENT_V1_SIZE \
    ((uint32_t)(offsetof(fly_session_event, reserved) + sizeof(uint32_t)))

typedef struct fly_session_command
{
    uint32_t struct_size;
    uint32_t version;
    uint64_t command_id;
    uint64_t transition_id;
    uint32_t kind;
    uint32_t reserved;
} fly_session_command;

#define FLY_SESSION_COMMAND_V1_SIZE \
    ((uint32_t)(offsetof(fly_session_command, reserved) + sizeof(uint32_t)))

typedef struct fly_session_command_result
{
    uint32_t struct_size;
    uint32_t version;
    uint64_t command_id;
    uint64_t transition_id;
    int32_t result;
    uint32_t reserved;
} fly_session_command_result;

/*
 * result.result follows fly_result_code: FLY_RESULT_OK (0) reports success, and
 * any other value is an executor failure. A failed or stale completion is
 * rejected as terminal and never authorizes an effect. transition_id must be 0;
 * a 128-bit wire transition id is not representable here and is never truncated
 * into this field.
 */

#define FLY_SESSION_COMMAND_RESULT_V1_SIZE \
    ((uint32_t)(offsetof(fly_session_command_result, reserved) + sizeof(uint32_t)))

typedef struct fly_session_snapshot
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t ui_state;
    uint32_t authority_role;
    uint32_t mode;
    uint32_t reserved;
    fly_frame_cursor_v1 committed_through;
    fly_evidence_cursor_v1 state_verified_through;
} fly_session_snapshot;

#define FLY_SESSION_SNAPSHOT_V1_SIZE \
    ((uint32_t)(offsetof(fly_session_snapshot, state_verified_through) + \
                sizeof(fly_evidence_cursor_v1)))

/*
 * 2026-09-13 invite-code amendment: additive snapshot of the invite-code
 * lookup route (one joiner attempt and one host invitation at most). All
 * zero values mean "no route state". join_phase values are the shared route
 * contract: 0 Idle, 1 LookingUp, 2 WaitingHostApproval, 3 WaitingSasConfirm,
 * 4 Authenticated, 5 Cancelled, 6 Expired, 7 Ambiguous. host_phase: 0 Idle,
 * 1 Active. reserved_zero must be zero on input and is written zero.
 */
#define FLY_SESSION_INVITE_SNAPSHOT_VERSION_1 UINT32_C(1)

typedef struct fly_session_invite_snapshot_v1
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t join_phase;
    uint32_t host_phase;
    uint64_t join_attempt_id;
    uint64_t host_generation;
    uint32_t host_attempts_left;
    uint32_t reserved_zero;
} fly_session_invite_snapshot_v1;

#define FLY_SESSION_INVITE_SNAPSHOT_V1_SIZE \
    ((uint32_t)(offsetof(fly_session_invite_snapshot_v1, reserved_zero) + \
                sizeof(uint32_t)))

FLYNES_API fly_result fly_session_create(const fly_session_config* config,
                                         fly_session_t** session_out);
FLYNES_API void fly_session_destroy(fly_session_t* session);

FLYNES_API fly_result fly_session_submit_event(fly_session_t* session,
                                               const fly_session_event* event);

FLYNES_API fly_result fly_session_receive_stream(fly_session_t* session,
                                                 uint32_t channel,
                                                 const uint8_t* bytes,
                                                 size_t size);
FLYNES_API fly_result fly_session_receive_datagram(fly_session_t* session,
                                                   uint32_t channel,
                                                   const uint8_t* bytes,
                                                   size_t size);

/*
 * Copies the immutable pending command, if any, into command_out.
 * A nonzero command_id together with kind == FLY_SESSION_COMMAND_NONE means a
 * command is pending whose kind has no v1 representation; command_id == 0 means
 * nothing is pending. Polling repeats the same id until it is completed.
 * transition_id is always 0: the wire transition id is not representable in
 * this 64-bit field and is never truncated into it.
 */
FLYNES_API fly_result fly_session_poll_command(fly_session_t* session,
                                               fly_session_command* command_out);
/*
 * Completes exactly the command_id returned by the last poll. Stale, duplicate,
 * unpolled or failed completions are rejected; the reducer's own generation
 * fence decides staleness.
 */
FLYNES_API fly_result fly_session_complete_command(
    fly_session_t* session,
    const fly_session_command_result* result);

FLYNES_API fly_result fly_session_tick(fly_session_t* session, uint64_t now_ns);

FLYNES_API fly_result fly_session_get_snapshot(fly_session_t* session,
                                               fly_session_snapshot* snapshot_out);

/*
 * 2026-09-13 invite-code amendment: additive projection of the invite-code
 * lookup route. Read-only; never authorizes an effect by itself.
 */
FLYNES_API fly_result fly_session_get_invite_snapshot(
    fly_session_t* session, fly_session_invite_snapshot_v1* snapshot_out);

#ifdef __cplusplus
}
#endif

#endif
