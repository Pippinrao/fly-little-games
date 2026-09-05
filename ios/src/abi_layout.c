#include <flynes/flynes_app.h>
#include <nes/nes.h>

#include <stddef.h>
#include <stdint.h>

_Static_assert(sizeof(void*) == 8u, "Stage-1 supports only 64-bit arm64 consumers");
_Static_assert(sizeof(size_t) == 8u, "Stage-1 requires a 64-bit size_t");
_Static_assert(sizeof(fly_result) == 4u, "fly_result ABI changed");
_Static_assert(sizeof(fly_platform_capabilities) == 16u, "fly capabilities ABI changed");
_Static_assert(FLY_PLATFORM_CAPABILITIES_V1_SIZE == 16u, "fly capabilities V1 changed");
_Static_assert(sizeof(fly_app_config) == 40u, "fly app config ABI changed");
_Static_assert(FLY_APP_CONFIG_V1_SIZE == 40u, "fly app config V1 changed");
_Static_assert(sizeof(fly_catalog_entry) == 256u, "fly catalog entry ABI changed");
_Static_assert(FLY_CATALOG_ENTRY_V1_SIZE == 256u, "fly catalog entry V1 changed");
_Static_assert(offsetof(fly_catalog_entry, source_uuid) == 8u,
               "fly catalog source UUID moved");
_Static_assert(offsetof(fly_catalog_entry, source_relative_path_utf8) == 240u,
               "fly catalog relative path pointer moved");
_Static_assert(sizeof(fly_scan_config) == 28u, "fly scan config ABI changed");
_Static_assert(FLY_SCAN_CONFIG_V1_SIZE == 28u, "fly scan config V1 changed");
_Static_assert(sizeof(fly_scan_file) == 88u, "fly scan file ABI changed");
_Static_assert(FLY_SCAN_FILE_V1_SIZE == 88u, "fly scan file V1 changed");
_Static_assert(sizeof(fly_scan_file_result) == 24u, "fly scan file result ABI changed");
_Static_assert(FLY_SCAN_FILE_RESULT_V1_SIZE == 24u, "fly scan file result V1 changed");

_Static_assert(sizeof(nes_config) == 20u, "nes_config ABI changed");
_Static_assert(_Alignof(nes_config) == 4u, "nes_config alignment changed");
_Static_assert(sizeof(nes_caps) == 36u, "nes_caps ABI changed");
_Static_assert(_Alignof(nes_caps) == 4u, "nes_caps alignment changed");
_Static_assert(sizeof(nes_video_frame) == 32u, "nes_video_frame ABI changed");
_Static_assert(_Alignof(nes_video_frame) == 8u, "nes_video_frame alignment changed");
_Static_assert(offsetof(nes_video_frame, pixels) == 24u,
               "nes_video_frame pixel pointer moved");
_Static_assert(sizeof(nes_video_snapshot) == 56u, "nes_video_snapshot ABI changed");
_Static_assert(_Alignof(nes_video_snapshot) == 8u, "nes_video_snapshot alignment changed");
_Static_assert(NES_VIDEO_SNAPSHOT_V1_SIZE == 40u, "nes video snapshot V1 changed");
_Static_assert(offsetof(nes_video_snapshot, sequence) == 8u,
               "nes video sequence moved");
_Static_assert(offsetof(nes_video_snapshot, bytes_written) == 32u,
               "nes video byte count moved");
_Static_assert(offsetof(nes_video_snapshot, source_region) == 40u,
               "nes video source region moved");
_Static_assert(offsetof(nes_video_snapshot, native_monotonic_ns) == 48u,
               "nes video timestamp moved");
_Static_assert(sizeof(nes_input_sample) == 40u, "nes_input_sample ABI changed");
_Static_assert(_Alignof(nes_input_sample) == 8u, "nes_input_sample alignment changed");
_Static_assert(sizeof(nes_frame_step_result) == 32u, "nes_frame_step_result ABI changed");
_Static_assert(_Alignof(nes_frame_step_result) == 8u,
               "nes_frame_step_result alignment changed");
_Static_assert(sizeof(nes_rom_info) == 640u, "nes_rom_info ABI changed");
_Static_assert(_Alignof(nes_rom_info) == 4u, "nes_rom_info alignment changed");
_Static_assert(offsetof(nes_rom_info, sha1) == 584u, "nes_rom_info sha1 moved");
_Static_assert(offsetof(nes_rom_info, crc32) == 625u, "nes_rom_info crc32 moved");
_Static_assert(offsetof(nes_rom_info, players) == 636u, "nes_rom_info tail changed");
_Static_assert(sizeof(nes_favored_system) == 4u, "nes enum representation changed");
_Static_assert(sizeof(nes_pixfmt) == 4u, "nes enum representation changed");
_Static_assert(sizeof(nes_region) == 4u, "nes enum representation changed");

#if defined(__GNUC__)
__attribute__((visibility("hidden")))
#endif
int flynes_ios_abi_layout_is_current(void)
{
    fly_result (*const app_create)(const fly_app_config*, fly_app_t**) = &fly_app_create;
    void (*const app_destroy)(fly_app_t*) = &fly_app_destroy;
    fly_result (*const scan_begin)(fly_app_t*, const fly_scan_config*, fly_scan_t**) =
        &fly_scan_begin;
    fly_result (*const scan_add)(fly_scan_t*, const fly_scan_file*, fly_scan_file_result*) =
        &fly_scan_add_file;
    fly_result (*const scan_commit)(fly_scan_t*, uint32_t) = &fly_scan_commit;
    void (*const scan_abort)(fly_scan_t*) = &fly_scan_abort;
    nes_t* (*const core_create)(const nes_config*) = &nes_create;
    void (*const core_destroy)(nes_t*) = &nes_destroy;
    int (*const load_rom)(nes_t*, const uint8_t*, size_t, nes_rom_info*) = &nes_load_rom;
    int (*const run_frames)(nes_t*, uint32_t, int16_t*, uint32_t, uint32_t*, uint32_t*) =
        &nes_run_frames;
    void (*const set_input)(nes_t*, uint32_t, uint32_t) = &nes_set_input;
    int (*const save_state)(nes_t*, uint8_t*, size_t, size_t*, size_t*) = &nes_save_state;
    int (*const load_state)(nes_t*, const uint8_t*, size_t) = &nes_load_state;
    (void)app_create;
    (void)app_destroy;
    (void)scan_begin;
    (void)scan_add;
    (void)scan_commit;
    (void)scan_abort;
    (void)core_create;
    (void)core_destroy;
    (void)load_rom;
    (void)run_frames;
    (void)set_input;
    (void)save_state;
    (void)load_state;
    return 1;
}
