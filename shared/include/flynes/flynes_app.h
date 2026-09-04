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
    FLY_RESULT_INTERNAL_ERROR = -7
};

#define FLY_PLATFORM_CAPABILITIES_VERSION_1 UINT32_C(1)
#define FLY_APP_CONFIG_VERSION_1 UINT32_C(1)
#define FLY_CATALOG_ENTRY_VERSION_1 UINT32_C(1)

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
 * data_root_utf8 and cache_root_utf8 must each point to a non-empty,
 * NUL-terminated UTF-8 string. They are borrowed only for fly_app_create; a
 * successful call copies both strings, so the caller may immediately release
 * or modify its storage. platform_capabilities is borrowed for the call.
 */
typedef struct fly_app_config
{
    uint32_t struct_size;
    uint32_t version;
    const char* data_root_utf8;
    const char* cache_root_utf8;
    const fly_platform_capabilities* platform_capabilities;
} fly_app_config;

#define FLY_APP_CONFIG_V1_SIZE \
    ((uint32_t)(offsetof(fly_app_config, platform_capabilities) + \
                sizeof(const fly_platform_capabilities*)))

/*
 * Caller-provided catalog output. Before a successful indexed get, the caller
 * supplies struct_size/version plus the two buffer pointers and capacities.
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
    uint64_t entry_id;
    uint64_t content_size;
    uint32_t flags;
    uint32_t reserved;
    char* display_name_utf8;
    uint32_t display_name_capacity;
    uint32_t display_name_required;
    char* source_uri_utf8;
    uint32_t source_uri_capacity;
    uint32_t source_uri_required;
} fly_catalog_entry;

#define FLY_CATALOG_ENTRY_V1_SIZE \
    ((uint32_t)(offsetof(fly_catalog_entry, source_uri_required) + sizeof(uint32_t)))

/*
 * Creates an application instance. On every failure where app_out is non-NULL,
 * *app_out is set to NULL. Version 1 requires the complete V1 prefix of both
 * configuration structures; larger struct_size values are accepted.
 */
FLYNES_API fly_result fly_app_create(const fly_app_config* config, fly_app_t** app_out);

/* NULL-safe. */
FLYNES_API void fly_app_destroy(fly_app_t* app);

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
