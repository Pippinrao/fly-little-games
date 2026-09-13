#ifndef FLYNES_FLYNES_RUNTIME_H
#define FLYNES_FLYNES_RUNTIME_H

#include <flynes/flynes_app.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FLYNES_API
#define FLYNES_API
#endif

typedef struct fly_runtime_handle fly_runtime_t;

#define FLY_RUNTIME_CONFIG_VERSION_1 UINT32_C(1)
#define FLY_FRAME_INPUT_VERSION_1 UINT32_C(1)
#define FLY_FRAME_RESULT_VERSION_1 UINT32_C(1)
#define FLY_LATEST_FRAME_VERSION_1 UINT32_C(1)
#define FLY_PCM_BLOCK_VERSION_1 UINT32_C(1)
#define FLY_RUNTIME_CHECKPOINT_VERSION_1 UINT32_C(1)

#define FLY_RUNTIME_PORT_COUNT 4u
#define FLY_RUNTIME_ROLLBACK_SLOTS 12u
#define FLY_RUNTIME_FRAME_WIDTH 256u
#define FLY_RUNTIME_FRAME_HEIGHT 240u
#define FLY_RUNTIME_RGB565_BYTES \
    (FLY_RUNTIME_FRAME_WIDTH * FLY_RUNTIME_FRAME_HEIGHT * 2u)
#define FLY_RUNTIME_DEFAULT_SAMPLE_RATE UINT32_C(48000)

enum fly_runtime_pixel_format
{
    FLY_RUNTIME_PIXEL_FORMAT_RGB565 = 1
};

enum fly_runtime_clear_reason
{
    FLY_RUNTIME_CLEAR_STOP = 1,
    FLY_RUNTIME_CLEAR_PAUSE = 2,
    FLY_RUNTIME_CLEAR_PEER_DISCONNECT = 3,
    FLY_RUNTIME_CLEAR_SEAT_REASSIGN = 4
};

/*
 * sample_rate of 0 selects FLY_RUNTIME_DEFAULT_SAMPLE_RATE. reserved must be
 * zero. The config is borrowed only for fly_runtime_create.
 */
typedef struct fly_runtime_config
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t sample_rate;
    uint32_t reserved;
} fly_runtime_config;

#define FLY_RUNTIME_CONFIG_V1_SIZE \
    ((uint32_t)(offsetof(fly_runtime_config, reserved) + sizeof(uint32_t)))

/*
 * One-call, one-frame input bundle. capture_time_ns and receive_time_ns are
 * this-process monotonic diagnostics and must not affect core execution.
 */
typedef struct fly_frame_input_v1
{
    uint32_t struct_size;
    uint32_t version;
    uint64_t timeline_epoch;
    uint64_t frame_index;
    uint32_t buttons[FLY_RUNTIME_PORT_COUNT];
    uint64_t input_sequence[FLY_RUNTIME_PORT_COUNT];
    uint32_t predicted_port_mask;
    uint32_t reserved0;
    uint64_t batch_sequence;
    uint64_t capture_time_ns;
    uint64_t receive_time_ns;
} fly_frame_input_v1;

#define FLY_FRAME_INPUT_V1_SIZE \
    ((uint32_t)(offsetof(fly_frame_input_v1, receive_time_ns) + sizeof(uint64_t)))

typedef struct fly_frame_result_v1
{
    uint32_t struct_size;
    uint32_t version;
    uint64_t timeline_epoch;
    uint64_t frame_index;
    uint64_t post_state_frame;
    uint64_t frame_sequence;
    uint64_t audio_first_sample_sequence;
    uint64_t audio_last_sample_sequence;
    uint64_t applied_input_sequence[FLY_RUNTIME_PORT_COUNT];
    uint64_t source_time_ns;
    uint64_t produced_time_ns;
    uint32_t video_published;
    uint32_t pcm_published;
    uint32_t used_predicted_ports;
    uint32_t reserved;
} fly_frame_result_v1;

#define FLY_FRAME_RESULT_V1_SIZE \
    ((uint32_t)(offsetof(fly_frame_result_v1, reserved) + sizeof(uint32_t)))

typedef struct fly_latest_frame_v1
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t reserved;
    uint64_t timeline_epoch;
    uint64_t frame_index;
    uint64_t frame_sequence;
    uint64_t applied_input_sequence[FLY_RUNTIME_PORT_COUNT];
    uint64_t source_time_ns;
    uint64_t produced_time_ns;
    uint32_t bytes_written;
    uint32_t reserved1;
} fly_latest_frame_v1;

#define FLY_LATEST_FRAME_V1_SIZE \
    ((uint32_t)(offsetof(fly_latest_frame_v1, reserved1) + sizeof(uint32_t)))

typedef struct fly_pcm_block_v1
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t sample_count;
    uint32_t reserved;
    uint64_t first_sample_sequence;
    uint64_t media_time_ns;
} fly_pcm_block_v1;

#define FLY_PCM_BLOCK_V1_SIZE \
    ((uint32_t)(offsetof(fly_pcm_block_v1, media_time_ns) + sizeof(uint64_t)))

FLYNES_API fly_result fly_runtime_create(const fly_runtime_config* config,
                                         fly_runtime_t** runtime_out);
FLYNES_API void fly_runtime_destroy(fly_runtime_t* runtime);

/*
 * Loads one ROM from caller-owned bytes. expected_sha256 is optional; when
 * non-NULL it must point to 32 bytes of the full-file SHA-256.
 */
FLYNES_API fly_result fly_runtime_load_rom(fly_runtime_t* runtime,
                                           const uint8_t* bytes,
                                           size_t size,
                                           const uint8_t* expected_sha256);

FLYNES_API fly_result fly_runtime_step_frame(fly_runtime_t* runtime,
                                             const fly_frame_input_v1* input,
                                             fly_frame_result_v1* result);

FLYNES_API fly_result fly_runtime_clear_input_ports(fly_runtime_t* runtime,
                                                    uint32_t port_mask,
                                                    uint32_t reason);

FLYNES_API fly_result fly_runtime_copy_latest_frame(fly_runtime_t* runtime,
                                                    void* rgb565_out,
                                                    size_t cap,
                                                    fly_latest_frame_v1* meta_out);

/*
 * Attempts the runtime mutex once without waiting. If it is busy or the PCM
 * queue is empty, returns OK with sample_capacity zero-valued samples, sample_count
 * equal to sample_capacity, and first_sample_sequence/media_time_ns both zero.
 * This fallback silence does not consume queued PCM or advance producer time;
 * it is not canonical content. Zero sequence/time alone does not identify it.
 * Otherwise sample_count reports only the queued samples copied, which may be
 * less than sample_capacity; the unused output buffer is left untouched.
 */
FLYNES_API fly_result fly_runtime_pull_pcm(fly_runtime_t* runtime,
                                           int16_t* samples_out,
                                           uint32_t sample_capacity,
                                           fly_pcm_block_v1* block_out);

FLYNES_API fly_result fly_runtime_capture_rollback(fly_runtime_t* runtime,
                                                   uint32_t slot);
FLYNES_API fly_result fly_runtime_restore_rollback(fly_runtime_t* runtime,
                                                   uint32_t slot);

FLYNES_API fly_result fly_runtime_save_checkpoint(fly_runtime_t* runtime,
                                                  uint8_t* out,
                                                  size_t cap,
                                                  size_t* written,
                                                  size_t* needed);
FLYNES_API fly_result fly_runtime_load_checkpoint(fly_runtime_t* runtime,
                                                  const uint8_t* bytes,
                                                  size_t size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FLYNES_FLYNES_RUNTIME_H */
