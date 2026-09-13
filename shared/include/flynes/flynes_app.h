#ifndef FLYNES_FLYNES_APP_H
#define FLYNES_FLYNES_APP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FLYNES_API
#define FLYNES_API
#endif

/* Opaque handles. Their storage is owned by this library. */
typedef struct fly_app_handle fly_app_t;
typedef struct fly_catalog_snapshot_handle fly_catalog_snapshot_t;
typedef struct fly_scan_handle fly_scan_t;

/*
 * Result storage is fixed at 32 bits. The named values below are permanent ABI
 * values; new failures will receive new values rather than renumbering these.
 */
typedef int32_t fly_result;
enum fly_result_code
{
    FLY_RESULT_OK = 0,
    FLY_RESULT_INVALID_ARGUMENT = -1,
    FLY_RESULT_STRUCT_TOO_SMALL = -2,
    FLY_RESULT_UNSUPPORTED_VERSION = -3,
    FLY_RESULT_OUT_OF_RANGE = -4,
    FLY_RESULT_BUFFER_TOO_SMALL = -5,
    FLY_RESULT_OUT_OF_MEMORY = -6,
    FLY_RESULT_INTERNAL_ERROR = -7,
    FLY_RESULT_INVALID_STATE = -8,
    FLY_RESULT_CONFLICT = -9,
    FLY_RESULT_NOT_FOUND = -10,
    FLY_RESULT_FORBIDDEN = -11
};

#define FLY_PLATFORM_CAPABILITIES_VERSION_1 UINT32_C(1)
#define FLY_APP_CONFIG_VERSION_1 UINT32_C(1)
#define FLY_CATALOG_ENTRY_VERSION_1 UINT32_C(1)
#define FLY_SCAN_CONFIG_VERSION_1 UINT32_C(1)
#define FLY_SCAN_FILE_VERSION_1 UINT32_C(1)
#define FLY_SCAN_FILE_RESULT_VERSION_1 UINT32_C(1)
#define FLY_CATALOG_USER_STATE_VERSION_1 UINT32_C(1)
#define FLY_SOURCE_STATUS_VERSION_1 UINT32_C(1)
#define FLY_APP_ROOT_MAX_UTF8_BYTES UINT32_C(4096)
#define FLY_CANONICAL_ID_MAX_UTF8_BYTES UINT32_C(4096)
#define FLY_SCAN_RELATIVE_PATH_MAX_UTF8_BYTES UINT32_C(4096)
#define FLY_SCAN_DISPLAY_NAME_MAX_UTF8_BYTES UINT32_C(1024)
#define FLY_SCAN_MAX_PACKAGE_BYTES UINT64_C(8388608)
#define FLY_SCAN_MAX_PAYLOAD_BYTES UINT64_C(8388608)
#define FLY_SCAN_MAX_ZIP_ENTRIES UINT32_C(2048)
#define FLY_SCAN_MAX_ZIP_INFLATED_BYTES UINT64_C(33554432)
#define FLY_SCAN_MAX_ZIP_NAME_BYTES UINT32_C(1024)
#define FLY_SCAN_MAX_ZIP_COMPRESSION_RATIO UINT32_C(200)
#define FLY_SCAN_ZIP_RATIO_GUARD_BYTES UINT64_C(1048576)

/* Platform-neutral storage scopes. None implies a provider, transport, or trust level. */
enum fly_source_scope
{
    FLY_SOURCE_SCOPE_BUILTIN = 1,
    FLY_SOURCE_SCOPE_USER_DIRECTORY = 2,
    FLY_SOURCE_SCOPE_USER_FILE = 3,
    FLY_SOURCE_SCOPE_MANAGED_LIBRARY = 4
};

enum fly_scan_completeness
{
    FLY_SCAN_COMPLETENESS_FULL = 1,
    FLY_SCAN_COMPLETENESS_PARTIAL = 2,
    FLY_SCAN_COMPLETENESS_FATAL = 3
};

enum fly_scan_file_outcome
{
    FLY_SCAN_FILE_OUTCOME_INDEXED = 1,
    FLY_SCAN_FILE_OUTCOME_SKIPPED = 2,
    FLY_SCAN_FILE_OUTCOME_REJECTED = 3
};

enum fly_scan_file_reason
{
    FLY_SCAN_FILE_REASON_INDEXED = 1,
    FLY_SCAN_FILE_REASON_NO_CATALOGABLE_ROM = 2,
    FLY_SCAN_FILE_REASON_IO_ERROR = 3,
    FLY_SCAN_FILE_REASON_PACKAGE_LIMIT_EXCEEDED = 4,
    FLY_SCAN_FILE_REASON_HASH_MISMATCH = 5,
    FLY_SCAN_FILE_REASON_INVALID_ZIP = 6
};

enum fly_package_format
{
    FLY_PACKAGE_FORMAT_RAW = 1,
    FLY_PACKAGE_FORMAT_ZIP = 2
};

enum fly_rom_format
{
    FLY_ROM_FORMAT_INES = 1,
    FLY_ROM_FORMAT_NES2 = 2,
    FLY_ROM_FORMAT_FDS = 3,
    FLY_ROM_FORMAT_UNIF = 4
};

enum fly_compatibility_state
{
    FLY_COMPATIBILITY_PLAYABLE = 1,
    FLY_COMPATIBILITY_UNSUPPORTED = 2,
    FLY_COMPATIBILITY_INVALID = 3
};

/* These values mirror the current shared ROM parser and are append-only. */
enum fly_compatibility_reason
{
    FLY_COMPATIBILITY_REASON_PLAYABLE_NES = 1,
    FLY_COMPATIBILITY_REASON_FDS_BIOS_API_NOT_IMPLEMENTED = 2,
    FLY_COMPATIBILITY_REASON_UNIF_PRODUCT_DISABLED = 3,
    FLY_COMPATIBILITY_REASON_NES_HEADER_INVALID = 4,
    FLY_COMPATIBILITY_REASON_NES_ZERO_PRG = 5,
    FLY_COMPATIBILITY_REASON_NES_TRUNCATED = 6,
    FLY_COMPATIBILITY_REASON_NES_SIZE_OVERFLOW = 7,
    FLY_COMPATIBILITY_REASON_FDS_INVALID_HEADER = 8,
    FLY_COMPATIBILITY_REASON_FDS_INVALID_SIDE_COUNT = 9,
    FLY_COMPATIBILITY_REASON_FDS_TRUNCATED = 10,
    FLY_COMPATIBILITY_REASON_UNIF_INVALID_CHUNK = 11,
    FLY_COMPATIBILITY_REASON_UNIF_MISSING_PRG = 12
};

enum fly_catalog_freshness
{
    FLY_CATALOG_FRESHNESS_FRESH = 1,
    FLY_CATALOG_FRESHNESS_STALE = 2
};

#define FLY_CATALOG_ENTRY_FLAG_BATTERY UINT32_C(0x00000001)
#define FLY_CATALOG_ENTRY_FLAG_TRAINER UINT32_C(0x00000002)
#define FLY_CATALOG_ENTRY_FLAG_TRAILING_DATA UINT32_C(0x00000004)
#define FLY_CATALOG_ENTRY_FLAG_DIRTY_HEADER UINT32_C(0x00000008)
#define FLY_CATALOG_ENTRY_FLAG_ZIP_ENTRY UINT32_C(0x00000010)

#define FLY_SCAN_FILE_FLAG_EXPECTED_PHYSICAL_SHA256 UINT32_C(0x00000001)

/*
 * Platform-neutral feature bits. Version 1 defines no required bits; callers
 * set flags to zero. Unknown bits are ignored so capabilities can grow without
 * changing fly_app_create.
 */
typedef struct fly_platform_capabilities
{
    uint32_t struct_size;
    uint32_t version;
    uint64_t flags;
} fly_platform_capabilities;

#define FLY_PLATFORM_CAPABILITIES_V1_SIZE \
    ((uint32_t)(offsetof(fly_platform_capabilities, flags) + sizeof(uint64_t)))

/*
 * data_root_utf8 and cache_root_utf8 each point to a byte range described by
 * the corresponding uint32_t length field. Lengths exclude any optional NUL
 * terminator, must be in [1, FLY_APP_ROOT_MAX_UTF8_BYTES], and the ranges need
 * not be NUL-terminated. Each range must be strictly well-formed UTF-8 and may
 * not contain U+0000. The ranges and platform_capabilities are borrowed only
 * for fly_app_create; a successful call copies exactly the specified bytes.
 */
typedef struct fly_app_config
{
    uint32_t struct_size;
    uint32_t version;
    const char* data_root_utf8;
    const char* cache_root_utf8;
    const fly_platform_capabilities* platform_capabilities;
    uint32_t data_root_utf8_length;
    uint32_t cache_root_utf8_length;
} fly_app_config;

#define FLY_APP_CONFIG_V1_SIZE \
    ((uint32_t)(offsetof(fly_app_config, cache_root_utf8_length) + sizeof(uint32_t)))

/*
 * Caller-provided catalog output. One row identifies one ROM variant. Before
 * a successful indexed get, the caller supplies struct_size/version plus the
 * four buffer pointers and capacities.
 * On success, strings are NUL-terminated and each *_required value includes
 * that terminator. A NULL buffer with zero capacity is a valid size query. If
 * capacity is insufficient, FLY_RESULT_BUFFER_TOO_SMALL is returned and the
 * required sizes are reported. No string pointer is ever borrowed from the
 * library. Versioned tail fields may be added without changing the get symbol.
 */
typedef struct fly_catalog_entry
{
    uint32_t struct_size;
    uint32_t version;
    uint8_t source_uuid[16];
    uint64_t payload_size;
    uint64_t physical_size;
    uint64_t expected_bytes;
    uint64_t prg_bytes;
    uint64_t chr_bytes;
    int32_t mapper;
    int32_t submapper;
    uint32_t disk_sides;
    uint8_t payload_sha1[20];
    uint8_t payload_sha256[32];
    uint8_t physical_sha256[32];
    /* Network byte order, matching the eight hexadecimal CRC-32 digits. */
    uint8_t payload_crc32[4];
    uint32_t source_scope;
    uint32_t package_format;
    uint32_t rom_format;
    uint32_t compatibility_state;
    uint32_t compatibility_reason;
    uint32_t freshness;
    uint32_t flags;
    char* canonical_id_utf8;
    uint32_t canonical_id_capacity;
    uint32_t canonical_id_required;
    char* variant_id_utf8;
    uint32_t variant_id_capacity;
    uint32_t variant_id_required;
    char* display_name_utf8;
    uint32_t display_name_capacity;
    uint32_t display_name_required;
    char* source_relative_path_utf8;
    uint32_t source_relative_path_capacity;
    uint32_t source_relative_path_required;
} fly_catalog_entry;

#define FLY_CATALOG_ENTRY_V1_SIZE \
    ((uint32_t)(offsetof(fly_catalog_entry, source_relative_path_required) + sizeof(uint32_t)))

/*
 * source_uuid is application-generated and must not be all zero. source_scope
 * describes only portable storage ownership. MANAGED_LIBRARY is a generic
 * application-managed import area; it conveys no transport origin or trust.
 * A scan captures the current catalog generation at begin time.
 */
typedef struct fly_scan_config
{
    uint32_t struct_size;
    uint32_t version;
    uint8_t source_uuid[16];
    uint32_t source_scope;
} fly_scan_config;

#define FLY_SCAN_CONFIG_V1_SIZE \
    ((uint32_t)(offsetof(fly_scan_config, source_scope) + sizeof(uint32_t)))

/*
 * Every file has a canonical platform-neutral relative logical path, including
 * USER_FILE sources. Both text fields are explicit non-NUL byte ranges copied
 * during the call. The path is strictly relative, uses '/' separators, has no
 * empty, '.' or '..' segment, and is limited to 4096 UTF-8 bytes. Display name
 * is a single UTF-8 filename. borrowed_fd is synchronously read from offset
 * zero only during fly_scan_add_file; the library neither stores nor closes it.
 * declared_size and modified_time_hint_ns are non-authoritative hints and do
 * not participate in any identity. Actual input is bounded independently.
 */
typedef struct fly_scan_file
{
    uint32_t struct_size;
    uint32_t version;
    const char* source_relative_path_utf8;
    const char* display_name_utf8;
    uint32_t source_relative_path_utf8_length;
    uint32_t display_name_utf8_length;
    int32_t borrowed_fd;
    uint32_t flags;
    uint64_t declared_size;
    int64_t modified_time_hint_ns;
    uint8_t expected_physical_sha256[32];
} fly_scan_file;

#define FLY_SCAN_FILE_V1_SIZE \
    ((uint32_t)(offsetof(fly_scan_file, expected_physical_sha256) + 32u))

/*
 * A successful add call always writes this entire prefix. INDEXED has at least
 * one variant. SKIPPED means the file was safely inspected but held no catalogable
 * ROM. FDS and UNIF are catalogable as explicit UNSUPPORTED entries. REJECTED
 * means the legal relative path was fully accounted but content,
 * descriptor, hash, or package validation failed; commit preserves old entries
 * for that path as stale. A non-OK call leaves this structure unchanged and does
 * not append a candidate (an allocation/internal failure poisons the scan).
 */
typedef struct fly_scan_file_result
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t outcome;
    uint32_t reason;
    uint32_t variant_count;
    uint32_t reserved;
} fly_scan_file_result;

#define FLY_SCAN_FILE_RESULT_V1_SIZE \
    ((uint32_t)(offsetof(fly_scan_file_result, reserved) + sizeof(uint32_t)))

/*
 * Caller-owned user record for one canonical_id. favorite is 0 or 1.
 * favorite_revision and last_played_sequence are library-owned monotonic
 * counters. Unknown canonical IDs read as zeros without creating a row.
 */
typedef struct fly_catalog_user_state
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t favorite;
    uint32_t play_count;
    uint64_t favorite_revision;
    uint64_t last_played_sequence;
} fly_catalog_user_state;

#define FLY_CATALOG_USER_STATE_V1_SIZE \
    ((uint32_t)(offsetof(fly_catalog_user_state, last_played_sequence) + sizeof(uint64_t)))

/*
 * Portable source registry row. last_completeness is the last committed scan
 * completeness for this UUID. freshness is FRESH unless the live catalog still
 * holds a stale row for the source. No locator or URI is present.
 */
typedef struct fly_source_status
{
    uint32_t struct_size;
    uint32_t version;
    uint8_t source_uuid[16];
    uint32_t source_scope;
    uint32_t last_completeness;
    uint32_t freshness;
} fly_source_status;

#define FLY_SOURCE_STATUS_V1_SIZE \
    ((uint32_t)(offsetof(fly_source_status, freshness) + sizeof(uint32_t)))

/* Stable settings enumerations. Values are append-only and are not Java ordinals. */
enum fly_aspect_mode
{
    FLY_ASPECT_FOUR_BY_THREE = 1,
    FLY_ASPECT_SQUARE_PIXELS = 2,
    FLY_ASPECT_INTEGER_SCALE = 3
};

enum fly_video_quality_preset
{
    FLY_VIDEO_QUALITY_POWER_SAVER = 1,
    FLY_VIDEO_QUALITY_BALANCED = 2,
    FLY_VIDEO_QUALITY_EXTREME = 3,
    FLY_VIDEO_QUALITY_CUSTOM = 4
};

enum fly_refresh_policy
{
    FLY_REFRESH_FOLLOW_SYSTEM = 1,
    FLY_REFRESH_LEGACY_AUTO_INTEGER_MULTIPLE = 2,
    FLY_REFRESH_HZ_60 = 3,
    FLY_REFRESH_HZ_90 = 4,
    FLY_REFRESH_HZ_120 = 5
};

enum fly_temporal_mode
{
    FLY_TEMPORAL_NATIVE = 1,
    FLY_TEMPORAL_MOTION_INTERPOLATION = 2
};

enum fly_spatial_mode
{
    FLY_SPATIAL_NEAREST = 1,
    FLY_SPATIAL_SHARP_BILINEAR = 2,
    FLY_SPATIAL_MMPX = 3,
    FLY_SPATIAL_SCALEFX = 4
};

enum fly_post_effect
{
    FLY_POST_EFFECT_NONE = 1,
    FLY_POST_EFFECT_CRT = 2
};

enum fly_layout_preset
{
    FLY_LAYOUT_STANDARD_BA = 1,
    FLY_LAYOUT_MIRRORED_AB = 2
};

enum fly_direction_mode
{
    FLY_DIRECTION_JOYSTICK = 1,
    FLY_DIRECTION_FIXED_JOYSTICK = 2,
    FLY_DIRECTION_DPAD = 3
};

enum fly_haptic_level
{
    FLY_HAPTIC_OFF = 1,
    FLY_HAPTIC_LIGHT = 2,
    FLY_HAPTIC_STANDARD = 3,
    FLY_HAPTIC_STRONG = 4
};

enum fly_audio_focus_policy
{
    FLY_AUDIO_FOCUS_PAUSE = 1,
    FLY_AUDIO_FOCUS_DUCK = 2,
    FLY_AUDIO_FOCUS_IGNORE = 3
};

#define FLY_SETTINGS_SNAPSHOT_VERSION_1 UINT32_C(1)
#define FLY_SETTINGS_LOCALE_MAX_UTF8_BYTES UINT32_C(128)
#define FLY_SETTINGS_BUTTON_SCALE_MIN 0.80f
#define FLY_SETTINGS_BUTTON_SCALE_MAX 1.40f
#define FLY_SETTINGS_VERTICAL_OFFSET_MIN (-0.25f)
#define FLY_SETTINGS_VERTICAL_OFFSET_MAX 0.25f
#define FLY_SETTINGS_CONTROL_OPACITY_MIN 0.40f
#define FLY_SETTINGS_CONTROL_OPACITY_MAX 1.00f
#define FLY_SETTINGS_JOYSTICK_SCALE_MIN 0.80f
#define FLY_SETTINGS_JOYSTICK_SCALE_MAX 1.40f
#define FLY_SETTINGS_DEAD_ZONE_MIN 0.08f
#define FLY_SETTINGS_DEAD_ZONE_MAX 0.45f

/*
 * One complete settings snapshot. Get fills caller buffers and reports required
 * sizes including a NUL terminator. Apply copies locale_tag_utf8_length and
 * last_played_id_utf8_length as explicit UTF-8 ranges; a zero-length
 * last_played id is valid, locale must be non-empty. Invalid values fail
 * without changing prior settings.
 */
typedef struct fly_settings_snapshot
{
    uint32_t struct_size;
    uint32_t version;
    uint32_t aspect_mode;
    uint32_t video_quality_preset;
    uint32_t custom_refresh_policy;
    uint32_t custom_temporal_mode;
    uint32_t custom_spatial_mode;
    uint32_t custom_post_effect;
    uint32_t adaptive_protection;
    uint32_t layout_preset;
    uint32_t direction_mode;
    float button_scale;
    float vertical_offset;
    float control_opacity;
    float joystick_scale;
    float dead_zone;
    uint32_t haptic_level;
    uint32_t distinct_ab_haptics;
    uint32_t audio_enabled;
    uint32_t audio_focus_policy;
    uint32_t autosave_enabled;
    char* locale_tag_utf8;
    char* last_played_id_utf8;
    uint32_t locale_tag_capacity;
    uint32_t locale_tag_required;
    uint32_t locale_tag_utf8_length;
    uint32_t last_played_id_capacity;
    uint32_t last_played_id_required;
    uint32_t last_played_id_utf8_length;
} fly_settings_snapshot;

#define FLY_SETTINGS_SNAPSHOT_V1_SIZE \
    ((uint32_t)(offsetof(fly_settings_snapshot, last_played_id_utf8_length) + sizeof(uint32_t)))

/*
 * Creates an application instance. On every failure where app_out is non-NULL,
 * *app_out is set to NULL. Version 1 requires the complete V1 prefix of both
 * configuration structures; larger struct_size values are accepted.
 */
FLYNES_API fly_result fly_app_create(const fly_app_config* config, fly_app_t** app_out);

/* NULL-safe. */
FLYNES_API void fly_app_destroy(fly_app_t* app);

/*
 * Starts one optimistic in-memory scan transaction. On failure where scan_out
 * is non-NULL, *scan_out is cleared. Enumeration completeness is supplied only
 * at commit because a platform may discover it late. Larger known-prefix
 * structs are accepted.
 */
FLYNES_API fly_result fly_scan_begin(fly_app_t* app,
                                     const fly_scan_config* config,
                                     fly_scan_t** scan_out);

FLYNES_API fly_result fly_scan_add_file(fly_scan_t* scan,
                                        const fly_scan_file* file,
                                        fly_scan_file_result* result_out);

/*
 * Atomically publishes a new immutable generation using final_completeness.
 * FATAL discards this scan's candidates and marks every old source row stale;
 * PARTIAL applies accounted paths and preserves unmentioned rows as stale;
 * FULL applies accounted paths and removes unmentioned rows. For every mode,
 * an INDEXED path replaces all its old variants, SKIPPED removes all variants,
 * and REJECTED preserves all old variants as stale. A stale base generation
 * returns FLY_RESULT_CONFLICT and closes the transaction without publishing.
 * Every commit attempt closes the handle; reuse returns INVALID_STATE.
 */
FLYNES_API fly_result fly_scan_commit(fly_scan_t* scan, uint32_t final_completeness);

/* NULL-safe. Closes if needed and releases the scan handle, including after commit. */
FLYNES_API void fly_scan_abort(fly_scan_t* scan);

/*
 * Acquires an immutable catalog snapshot owned independently from the app.
 * On failure where snapshot_out is non-NULL, *snapshot_out is set to NULL.
 * The snapshot remains valid after fly_app_destroy and until explicit release.
 */
FLYNES_API fly_result fly_catalog_snapshot(const fly_app_t* app,
                                           fly_catalog_snapshot_t** snapshot_out);

FLYNES_API fly_result fly_catalog_snapshot_generation(
    const fly_catalog_snapshot_t* snapshot,
    uint64_t* generation_out);

FLYNES_API fly_result fly_catalog_snapshot_count(const fly_catalog_snapshot_t* snapshot,
                                                 uint64_t* count_out);

/* Additive display metadata; existing catalog entry and identity ABI is unchanged.
 * Strings are NUL-terminated UTF-8, library-owned and valid for process lifetime.
 * aliases_utf8 contains newline-separated aliases. Unknown results have empty
 * strings and match_kind=0; hash matches use 1 and explicit alias matches use 2.
 */
typedef struct fly_game_title
{
    const char* index_id_utf8;
    const char* title_en_utf8;
    const char* title_zh_hans_utf8;
    const char* aliases_utf8;
    uint32_t match_kind;
} fly_game_title;

/* NULL hash enables alias-only lookup; NULL name requires zero length. A lookup
 * miss returns OK with an empty result. Errors leave out unchanged. No ROM I/O.
 */
FLYNES_API fly_result fly_game_title_resolve(const uint8_t payload_sha256[32],
    const char* fallback_name_utf8, uint32_t fallback_name_utf8_length,
    fly_game_title* out);

/* Resolves the stored payload hash, then entry display name (inner ZIP member).
 * Invalid arguments/out-of-range indices leave out unchanged.
 */
FLYNES_API fly_result fly_catalog_snapshot_get_title(
    const fly_catalog_snapshot_t* snapshot, uint64_t index, fly_game_title* out);

/*
 * Retrieves one entry into caller-owned storage. An index outside the immutable
 * snapshot returns FLY_RESULT_OUT_OF_RANGE without modifying entry_out or its
 * buffers.
 */
FLYNES_API fly_result fly_catalog_snapshot_get(const fly_catalog_snapshot_t* snapshot,
                                               uint64_t index,
                                               fly_catalog_entry* entry_out);

/* Exact ZIP identity, independent of display-name decoding. Raw packages return
 * required=0 and offset=-1. BUFFER_TOO_SMALL writes required only; invalid arguments
 * and out-of-range indices leave all outputs untouched. No NUL terminator is added. */
FLYNES_API fly_result fly_catalog_snapshot_get_zip_locator(
    const fly_catalog_snapshot_t* snapshot, uint64_t index,
    uint8_t* raw_name, uint32_t capacity, uint32_t* required, int32_t* offset);

/* NULL-safe. */
FLYNES_API void fly_catalog_snapshot_release(fly_catalog_snapshot_t* snapshot);

/*
 * Reads the user record for one canonical_id. Missing IDs write the empty
 * record (favorite 0, counters 0) rather than OUT_OF_RANGE. The identifier is
 * an explicit UTF-8 byte range copied during the call.
 */
FLYNES_API fly_result fly_catalog_user_state_get(const fly_app_t* app,
                                                 const char* canonical_id_utf8,
                                                 uint32_t canonical_id_utf8_length,
                                                 fly_catalog_user_state* state_out);

/*
 * Sets favorite for canonical_id and assigns the next monotonic favorite
 * revision. favorite must be 0 or 1. Persists through FLYCAT01.
 */
FLYNES_API fly_result fly_catalog_favorite_set(fly_app_t* app,
                                               const char* canonical_id_utf8,
                                               uint32_t canonical_id_utf8_length,
                                               uint32_t favorite);

/*
 * Records a play against canonical_id: increments play_count and assigns the
 * next last_played_sequence. Does not launch a session. Persists through FLYCAT01.
 */
FLYNES_API fly_result fly_catalog_mark_played(fly_app_t* app,
                                              const char* canonical_id_utf8,
                                              uint32_t canonical_id_utf8_length);

FLYNES_API fly_result fly_source_status_count(const fly_app_t* app, uint64_t* count_out);

/*
 * Retrieves one source-registry row. An index outside the current registry
 * returns FLY_RESULT_OUT_OF_RANGE without modifying status_out.
 */
FLYNES_API fly_result fly_source_status_get(const fly_app_t* app,
                                            uint64_t index,
                                            fly_source_status* status_out);

/*
 * Removes one source-registry row and every catalog entry that belongs to it.
 * Builtin sources return FLY_RESULT_FORBIDDEN. Unknown UUID/scope pairs return
 * FLY_RESULT_NOT_FOUND. User-state rows are retained so favorites and recents
 * for surviving games remain intact. Successful removal publishes a new
 * generation through FLYCAT01.
 */
FLYNES_API fly_result fly_source_remove(fly_app_t* app,
                                        const uint8_t source_uuid[16],
                                        uint32_t source_scope);

FLYNES_API fly_result fly_settings_get(const fly_app_t* app, fly_settings_snapshot* snapshot_out);

/*
 * Replaces the entire settings snapshot atomically. On success the snapshot is
 * persisted to settings.flyset01 independently of the catalog file.
 */
FLYNES_API fly_result fly_settings_apply(fly_app_t* app, const fly_settings_snapshot* snapshot);

/*
 * Reads the persisted ControlLayoutV2 wire string. On success the output is
 * NUL-terminated and required_out includes that terminator. A NULL buffer with
 * zero capacity is a valid size query. Missing or invalid on-disk payloads
 * surface as the recommended layout encode.
 */
FLYNES_API fly_result fly_control_layout_get(const fly_app_t* app,
                                             char* utf8_out,
                                             uint32_t capacity,
                                             uint32_t* required_out);

/*
 * Applies one ControlLayoutV2 wire string. Invalid UTF-8 or payload content is
 * normalized through decode_or_recommended and persisted atomically to
 * control_layout.v2 beside settings.flyset01.
 */
FLYNES_API fly_result fly_control_layout_apply(fly_app_t* app,
                                               const char* utf8,
                                               uint32_t utf8_length);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FLYNES_FLYNES_APP_H */
