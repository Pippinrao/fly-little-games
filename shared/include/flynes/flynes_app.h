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
    FLY_RESULT_CONFLICT = -9
};

#define FLY_PLATFORM_CAPABILITIES_VERSION_1 UINT32_C(1)
#define FLY_APP_CONFIG_VERSION_1 UINT32_C(1)
#define FLY_CATALOG_ENTRY_VERSION_1 UINT32_C(1)
#define FLY_SCAN_CONFIG_VERSION_1 UINT32_C(1)
#define FLY_SCAN_FILE_VERSION_1 UINT32_C(1)
#define FLY_SCAN_FILE_RESULT_VERSION_1 UINT32_C(1)
#define FLY_APP_ROOT_MAX_UTF8_BYTES UINT32_C(4096)
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

/*
 * Retrieves one entry into caller-owned storage. An index outside the immutable
 * snapshot returns FLY_RESULT_OUT_OF_RANGE without modifying entry_out or its
 * buffers.
 */
FLYNES_API fly_result fly_catalog_snapshot_get(const fly_catalog_snapshot_t* snapshot,
                                               uint64_t index,
                                               fly_catalog_entry* entry_out);

/* NULL-safe. */
FLYNES_API void fly_catalog_snapshot_release(fly_catalog_snapshot_t* snapshot);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FLYNES_FLYNES_APP_H */
