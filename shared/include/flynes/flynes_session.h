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
    FLY_SESSION_COMMAND_NONE = 0
};

enum fly_session_ui_state
{
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

FLYNES_API fly_result fly_session_poll_command(fly_session_t* session,
                                               fly_session_command* command_out);
FLYNES_API fly_result fly_session_complete_command(
    fly_session_t* session,
    const fly_session_command_result* result);

FLYNES_API fly_result fly_session_tick(fly_session_t* session, uint64_t now_ns);

FLYNES_API fly_result fly_session_get_snapshot(fly_session_t* session,
                                               fly_session_snapshot* snapshot_out);

#ifdef __cplusplus
}
#endif

#endif
