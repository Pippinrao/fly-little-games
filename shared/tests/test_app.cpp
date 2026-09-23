#include <flynes/flynes_app.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

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

fly_platform_capabilities make_capabilities()
{
    fly_platform_capabilities capabilities{};
    capabilities.struct_size = FLY_PLATFORM_CAPABILITIES_V1_SIZE;
    capabilities.version = FLY_PLATFORM_CAPABILITIES_VERSION_1;
    capabilities.flags = 0;
    return capabilities;
}

fly_app_config make_config(const fly_platform_capabilities* capabilities)
{
    fly_app_config config{};
    config.struct_size = FLY_APP_CONFIG_V1_SIZE;
    config.version = FLY_APP_CONFIG_VERSION_1;
    config.data_root_utf8 = "test-data";
    config.cache_root_utf8 = "test-cache";
    config.platform_capabilities = capabilities;
    config.data_root_utf8_length = 9;
    config.cache_root_utf8_length = 10;
    return config;
}

void check_failed_create(const fly_app_config* config, fly_result expected, const char* message)
{
    fly_app_t* app = reinterpret_cast<fly_app_t*>(static_cast<std::uintptr_t>(1));
    const fly_result result = fly_app_create(config, &app);
    check(result == expected, message);
    check(app == nullptr, "failed create clears the output handle");
}

} // namespace

static_assert(sizeof(fly_result) == sizeof(std::int32_t), "fly_result must be 32-bit");
static_assert(FLY_RESULT_OK == 0, "FLY_RESULT_OK ABI value changed");
static_assert(FLY_RESULT_INVALID_ARGUMENT == -1, "FLY_RESULT_INVALID_ARGUMENT ABI value changed");
static_assert(FLY_RESULT_STRUCT_TOO_SMALL == -2, "FLY_RESULT_STRUCT_TOO_SMALL ABI value changed");
static_assert(FLY_RESULT_UNSUPPORTED_VERSION == -3, "FLY_RESULT_UNSUPPORTED_VERSION ABI value changed");
static_assert(FLY_RESULT_OUT_OF_RANGE == -4, "FLY_RESULT_OUT_OF_RANGE ABI value changed");
static_assert(FLY_RESULT_BUFFER_TOO_SMALL == -5, "FLY_RESULT_BUFFER_TOO_SMALL ABI value changed");
static_assert(FLY_RESULT_OUT_OF_MEMORY == -6, "FLY_RESULT_OUT_OF_MEMORY ABI value changed");
static_assert(FLY_RESULT_INTERNAL_ERROR == -7, "FLY_RESULT_INTERNAL_ERROR ABI value changed");
static_assert(FLY_RESULT_INVALID_STATE == -8, "FLY_RESULT_INVALID_STATE ABI value changed");
static_assert(FLY_RESULT_CONFLICT == -9, "FLY_RESULT_CONFLICT ABI value changed");
static_assert(FLY_PLATFORM_CAPABILITIES_VERSION_1 == 1,
              "capabilities version value changed");
static_assert(FLY_APP_CONFIG_VERSION_1 == 1, "config version value changed");
static_assert(FLY_CATALOG_ENTRY_VERSION_1 == 1, "entry version value changed");
static_assert(FLY_APP_ROOT_MAX_UTF8_BYTES == 4096, "root byte limit changed");

static_assert(offsetof(fly_platform_capabilities, struct_size) == 0, "capabilities prefix changed");
static_assert(offsetof(fly_platform_capabilities, version) == sizeof(std::uint32_t),
              "capabilities version offset changed");
static_assert(offsetof(fly_platform_capabilities, flags) == 2 * sizeof(std::uint32_t),
              "capabilities flags offset changed");
static_assert(sizeof(fly_platform_capabilities) == 16, "capabilities v1 layout changed");
static_assert(FLY_PLATFORM_CAPABILITIES_V1_SIZE == sizeof(fly_platform_capabilities),
              "capabilities v1 prefix size changed");

static_assert(offsetof(fly_app_config, struct_size) == 0, "config prefix changed");
static_assert(offsetof(fly_app_config, version) == sizeof(std::uint32_t),
              "config version offset changed");
static_assert(offsetof(fly_app_config, data_root_utf8) == 2 * sizeof(std::uint32_t),
              "config data-root offset changed");
static_assert(offsetof(fly_app_config, cache_root_utf8) ==
                  2 * sizeof(std::uint32_t) + sizeof(const char*),
              "config cache-root offset changed");
static_assert(offsetof(fly_app_config, platform_capabilities) ==
                  2 * sizeof(std::uint32_t) + 2 * sizeof(const char*),
              "config capabilities offset changed");
static_assert(offsetof(fly_app_config, data_root_utf8_length) ==
                  2 * sizeof(std::uint32_t) + 3 * sizeof(const void*),
              "config data-root length offset changed");
static_assert(offsetof(fly_app_config, cache_root_utf8_length) ==
                  3 * sizeof(std::uint32_t) + 3 * sizeof(const void*),
              "config cache-root length offset changed");
static_assert(sizeof(fly_app_config) == 4 * sizeof(std::uint32_t) + 3 * sizeof(const void*),
              "config v1 layout changed");
static_assert(FLY_APP_CONFIG_V1_SIZE == sizeof(fly_app_config),
              "config v1 prefix size changed");

static_assert(offsetof(fly_catalog_entry, struct_size) == 0, "entry prefix changed");
static_assert(offsetof(fly_catalog_entry, version) == sizeof(std::uint32_t),
              "entry version offset changed");
static_assert(offsetof(fly_catalog_entry, source_uuid) == 2 * sizeof(std::uint32_t),
              "entry source UUID offset changed");
static_assert(offsetof(fly_catalog_entry, payload_size) ==
                  2 * sizeof(std::uint32_t) + 16,
              "entry payload-size offset changed");
static_assert(offsetof(fly_catalog_entry, physical_size) ==
                  offsetof(fly_catalog_entry, payload_size) + sizeof(std::uint64_t),
              "entry physical-size offset changed");
static_assert(offsetof(fly_catalog_entry, expected_bytes) ==
                  offsetof(fly_catalog_entry, physical_size) + sizeof(std::uint64_t),
              "entry expected-bytes offset changed");
static_assert(offsetof(fly_catalog_entry, prg_bytes) ==
                  offsetof(fly_catalog_entry, expected_bytes) + sizeof(std::uint64_t),
              "entry PRG-bytes offset changed");
static_assert(offsetof(fly_catalog_entry, chr_bytes) ==
                  offsetof(fly_catalog_entry, prg_bytes) + sizeof(std::uint64_t),
              "entry CHR-bytes offset changed");
static_assert(offsetof(fly_catalog_entry, mapper) ==
                  offsetof(fly_catalog_entry, chr_bytes) + sizeof(std::uint64_t),
              "entry mapper offset changed");
static_assert(offsetof(fly_catalog_entry, submapper) ==
                  offsetof(fly_catalog_entry, mapper) + sizeof(std::int32_t),
              "entry submapper offset changed");
static_assert(offsetof(fly_catalog_entry, disk_sides) ==
                  offsetof(fly_catalog_entry, submapper) + sizeof(std::int32_t),
              "entry disk-sides offset changed");
static_assert(offsetof(fly_catalog_entry, payload_sha1) ==
                  offsetof(fly_catalog_entry, disk_sides) + sizeof(std::uint32_t),
              "entry payload SHA-1 offset changed");
static_assert(offsetof(fly_catalog_entry, payload_sha256) ==
                  offsetof(fly_catalog_entry, payload_sha1) + 20,
              "entry payload SHA-256 offset changed");
static_assert(offsetof(fly_catalog_entry, physical_sha256) ==
                  offsetof(fly_catalog_entry, payload_sha256) + 32,
              "entry physical SHA-256 offset changed");
static_assert(offsetof(fly_catalog_entry, payload_crc32) ==
                  offsetof(fly_catalog_entry, physical_sha256) + 32,
              "entry payload CRC-32 offset changed");
static_assert(offsetof(fly_catalog_entry, source_scope) ==
                  offsetof(fly_catalog_entry, payload_crc32) + 4,
              "entry source-scope offset changed");
static_assert(offsetof(fly_catalog_entry, package_format) ==
                  offsetof(fly_catalog_entry, source_scope) + sizeof(std::uint32_t),
              "entry package-format offset changed");
static_assert(offsetof(fly_catalog_entry, rom_format) ==
                  offsetof(fly_catalog_entry, package_format) + sizeof(std::uint32_t),
              "entry ROM-format offset changed");
static_assert(offsetof(fly_catalog_entry, compatibility_state) ==
                  offsetof(fly_catalog_entry, rom_format) + sizeof(std::uint32_t),
              "entry compatibility-state offset changed");
static_assert(offsetof(fly_catalog_entry, compatibility_reason) ==
                  offsetof(fly_catalog_entry, compatibility_state) + sizeof(std::uint32_t),
              "entry compatibility-reason offset changed");
static_assert(offsetof(fly_catalog_entry, freshness) ==
                  offsetof(fly_catalog_entry, compatibility_reason) + sizeof(std::uint32_t),
              "entry freshness offset changed");
static_assert(offsetof(fly_catalog_entry, flags) ==
                  offsetof(fly_catalog_entry, freshness) + sizeof(std::uint32_t),
              "entry flags offset changed");
static_assert(offsetof(fly_catalog_entry, canonical_id_utf8) ==
                  offsetof(fly_catalog_entry, flags) + sizeof(std::uint32_t),
              "entry canonical-id pointer offset changed");
static_assert(offsetof(fly_catalog_entry, canonical_id_capacity) ==
                  offsetof(fly_catalog_entry, canonical_id_utf8) + sizeof(char*),
              "entry canonical-id capacity offset changed");
static_assert(offsetof(fly_catalog_entry, canonical_id_required) ==
                  offsetof(fly_catalog_entry, canonical_id_capacity) + sizeof(std::uint32_t),
              "entry canonical-id required-size offset changed");
static_assert(offsetof(fly_catalog_entry, variant_id_utf8) ==
                  offsetof(fly_catalog_entry, canonical_id_required) + sizeof(std::uint32_t),
              "entry variant-id pointer offset changed");
static_assert(offsetof(fly_catalog_entry, variant_id_capacity) ==
                  offsetof(fly_catalog_entry, variant_id_utf8) + sizeof(char*),
              "entry variant-id capacity offset changed");
static_assert(offsetof(fly_catalog_entry, variant_id_required) ==
                  offsetof(fly_catalog_entry, variant_id_capacity) + sizeof(std::uint32_t),
              "entry variant-id required-size offset changed");
static_assert(offsetof(fly_catalog_entry, display_name_utf8) ==
                  offsetof(fly_catalog_entry, variant_id_required) + sizeof(std::uint32_t),
              "entry display-name pointer offset changed");
static_assert(offsetof(fly_catalog_entry, display_name_capacity) ==
                  offsetof(fly_catalog_entry, display_name_utf8) + sizeof(char*),
              "entry display-name capacity offset changed");
static_assert(offsetof(fly_catalog_entry, display_name_required) ==
                  offsetof(fly_catalog_entry, display_name_capacity) + sizeof(std::uint32_t),
              "entry display-name required-size offset changed");
static_assert(offsetof(fly_catalog_entry, source_relative_path_utf8) ==
                  offsetof(fly_catalog_entry, display_name_required) + sizeof(std::uint32_t),
              "entry relative-path pointer offset changed");
static_assert(offsetof(fly_catalog_entry, source_relative_path_capacity) ==
                  offsetof(fly_catalog_entry, source_relative_path_utf8) + sizeof(char*),
              "entry relative-path capacity offset changed");
static_assert(offsetof(fly_catalog_entry, source_relative_path_required) ==
                  offsetof(fly_catalog_entry, source_relative_path_capacity) +
                      sizeof(std::uint32_t),
              "entry relative-path required-size offset changed");
static_assert(FLY_CATALOG_ENTRY_V1_SIZE == sizeof(fly_catalog_entry),
              "catalog entry v1 prefix size changed");
static_assert(offsetof(fly_scan_config, source_uuid) == 2 * sizeof(std::uint32_t),
              "scan config UUID offset changed");
static_assert(FLY_SCAN_CONFIG_V1_SIZE == sizeof(fly_scan_config),
              "scan config v1 prefix size changed");
static_assert(FLY_SCAN_FILE_V1_SIZE == sizeof(fly_scan_file),
              "scan file v1 prefix size changed");
static_assert(FLY_SCAN_FILE_RESULT_V1_SIZE == sizeof(fly_scan_file_result),
              "scan file result v1 prefix size changed");

int main()
{
    fly_platform_capabilities capabilities = make_capabilities();
    fly_app_config config = make_config(&capabilities);

    check(fly_app_create(nullptr, nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "create rejects null config and output");

    fly_app_t* app = reinterpret_cast<fly_app_t*>(static_cast<std::uintptr_t>(1));
    check(fly_app_create(nullptr, &app) == FLY_RESULT_INVALID_ARGUMENT,
          "create rejects a null config");
    check(app == nullptr, "null-config create clears the output handle");
    check(fly_app_create(&config, nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "create rejects a null output pointer");

    fly_app_config bad_config = config;
    bad_config.struct_size = FLY_APP_CONFIG_V1_SIZE - 1u;
    check_failed_create(&bad_config, FLY_RESULT_STRUCT_TOO_SMALL,
                        "create rejects a short config");

    bad_config = config;
    bad_config.version = FLY_APP_CONFIG_VERSION_1 + 1u;
    check_failed_create(&bad_config, FLY_RESULT_UNSUPPORTED_VERSION,
                        "create rejects an unknown config version");

    fly_platform_capabilities bad_capabilities = capabilities;
    bad_capabilities.struct_size = FLY_PLATFORM_CAPABILITIES_V1_SIZE - 1u;
    bad_config = make_config(&bad_capabilities);
    check_failed_create(&bad_config, FLY_RESULT_STRUCT_TOO_SMALL,
                        "create rejects short platform capabilities");

    bad_capabilities = capabilities;
    bad_capabilities.version = FLY_PLATFORM_CAPABILITIES_VERSION_1 + 1u;
    bad_config = make_config(&bad_capabilities);
    check_failed_create(&bad_config, FLY_RESULT_UNSUPPORTED_VERSION,
                        "create rejects an unknown capabilities version");

    bad_config = config;
    bad_config.platform_capabilities = nullptr;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects missing platform capabilities");

    bad_config = config;
    bad_config.data_root_utf8 = nullptr;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects a null data root with a positive length");
    bad_config = config;
    bad_config.data_root_utf8_length = 0;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects a zero-length data root");

    bad_config = config;
    bad_config.cache_root_utf8 = nullptr;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects a null cache root with a positive length");
    bad_config = config;
    bad_config.cache_root_utf8_length = 0;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects a zero-length cache root");

    const char non_terminated_data_root[] = {'d', 'a', 't', 'a', '!', '\0'};
    const char non_terminated_cache_root[] = {'c', 'a', 'c', 'h', 'e', '!', '\0'};
    fly_app_config ranged_config = config;
    ranged_config.data_root_utf8 = non_terminated_data_root;
    ranged_config.data_root_utf8_length = 4;
    ranged_config.cache_root_utf8 = non_terminated_cache_root;
    ranged_config.cache_root_utf8_length = 5;
    fly_app_t* ranged_app = nullptr;
    check(fly_app_create(&ranged_config, &ranged_app) == FLY_RESULT_OK,
          "create accepts valid byte ranges without a terminator at the range boundary");
    check(ranged_app != nullptr, "byte-range create returns an app handle");
    fly_app_destroy(ranged_app);

    const char multibyte_root[] = {
        static_cast<char>(0xC2), static_cast<char>(0xA2),
        static_cast<char>(0xE2), static_cast<char>(0x82), static_cast<char>(0xAC),
        static_cast<char>(0xF0), static_cast<char>(0x9F), static_cast<char>(0x9A),
        static_cast<char>(0x80), '!', '\0'};
    ranged_config = config;
    ranged_config.data_root_utf8 = multibyte_root;
    ranged_config.data_root_utf8_length = 9;
    ranged_app = nullptr;
    check(fly_app_create(&ranged_config, &ranged_app) == FLY_RESULT_OK,
          "create accepts well-formed two-, three-, and four-byte UTF-8");
    check(ranged_app != nullptr, "multibyte UTF-8 create returns an app handle");
    fly_app_destroy(ranged_app);

    std::array<char, FLY_APP_ROOT_MAX_UTF8_BYTES + 1u> maximum_root{};
    maximum_root.fill('a');
    maximum_root[FLY_APP_ROOT_MAX_UTF8_BYTES] = '\0';
    ranged_config = config;
    ranged_config.data_root_utf8 = maximum_root.data();
    ranged_config.data_root_utf8_length = FLY_APP_ROOT_MAX_UTF8_BYTES;
    ranged_app = nullptr;
    check(fly_app_create(&ranged_config, &ranged_app) == FLY_RESULT_OK,
          "create accepts a root exactly at the UTF-8 byte limit");
    check(ranged_app != nullptr, "maximum-length create returns an app handle");
    fly_app_destroy(ranged_app);

    std::array<char, FLY_APP_ROOT_MAX_UTF8_BYTES + 2u> oversized_root{};
    oversized_root.fill('a');
    oversized_root[FLY_APP_ROOT_MAX_UTF8_BYTES + 1u] = '\0';
    bad_config = config;
    bad_config.data_root_utf8 = oversized_root.data();
    bad_config.data_root_utf8_length = FLY_APP_ROOT_MAX_UTF8_BYTES + 1u;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects a root over the UTF-8 byte limit");

    const char embedded_nul[] = {'a', '\0', 'b', '\0'};
    bad_config = config;
    bad_config.data_root_utf8 = embedded_nul;
    bad_config.data_root_utf8_length = 3;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects an embedded NUL");

    const char stray_continuation[] = {static_cast<char>(0x80), '\0'};
    bad_config = config;
    bad_config.data_root_utf8 = stray_continuation;
    bad_config.data_root_utf8_length = 1;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects a stray UTF-8 continuation byte");

    const char malformed_sequence[] = {
        static_cast<char>(0xE2), '(', static_cast<char>(0xA1), '\0'};
    bad_config = config;
    bad_config.data_root_utf8 = malformed_sequence;
    bad_config.data_root_utf8_length = 3;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects a malformed UTF-8 continuation sequence");

    const char truncated_sequence[] = {
        static_cast<char>(0xE2), static_cast<char>(0x82), '\0'};
    bad_config = config;
    bad_config.data_root_utf8 = truncated_sequence;
    bad_config.data_root_utf8_length = 2;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects truncated UTF-8");

    const char overlong_sequence[] = {
        static_cast<char>(0xC0), static_cast<char>(0xAF), '\0'};
    bad_config = config;
    bad_config.data_root_utf8 = overlong_sequence;
    bad_config.data_root_utf8_length = 2;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects overlong UTF-8");

    const char surrogate_sequence[] = {
        static_cast<char>(0xED), static_cast<char>(0xA0), static_cast<char>(0x80), '\0'};
    bad_config = config;
    bad_config.data_root_utf8 = surrogate_sequence;
    bad_config.data_root_utf8_length = 3;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects UTF-8 encoding a surrogate code point");

    const char out_of_range_sequence[] = {
        static_cast<char>(0xF4), static_cast<char>(0x90),
        static_cast<char>(0x80), static_cast<char>(0x80), '\0'};
    bad_config = config;
    bad_config.data_root_utf8 = out_of_range_sequence;
    bad_config.data_root_utf8_length = 4;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects UTF-8 above U+10FFFF");

    fly_platform_capabilities unknown_capabilities = capabilities;
    unknown_capabilities.flags = UINT64_MAX;
    fly_app_config unknown_capabilities_config = make_config(&unknown_capabilities);
    fly_app_t* unknown_capabilities_app = nullptr;
    check(fly_app_create(&unknown_capabilities_config, &unknown_capabilities_app) ==
              FLY_RESULT_OK,
          "create accepts and ignores unknown platform-capability bits");
    check(unknown_capabilities_app != nullptr,
          "unknown capability bits still produce an app handle");
    fly_app_destroy(unknown_capabilities_app);

    fly_platform_capabilities extended_capabilities = capabilities;
    extended_capabilities.struct_size = FLY_PLATFORM_CAPABILITIES_V1_SIZE + 16u;
    fly_app_config extended_config = make_config(&extended_capabilities);
    extended_config.struct_size = FLY_APP_CONFIG_V1_SIZE + 16u;
    app = nullptr;
    check(fly_app_create(&extended_config, &app) == FLY_RESULT_OK,
          "create accepts structs with a complete known prefix");
    check(app != nullptr, "successful create returns an app handle");

    check(fly_catalog_snapshot(nullptr, nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "snapshot acquisition rejects null arguments");
    fly_catalog_snapshot_t* snapshot =
        reinterpret_cast<fly_catalog_snapshot_t*>(static_cast<std::uintptr_t>(1));
    check(fly_catalog_snapshot(nullptr, &snapshot) == FLY_RESULT_INVALID_ARGUMENT,
          "snapshot acquisition rejects a null app");
    check(snapshot == nullptr, "failed snapshot acquisition clears the output handle");
    check(fly_catalog_snapshot(app, nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "snapshot acquisition rejects a null output pointer");

    snapshot = nullptr;
    check(fly_catalog_snapshot(app, &snapshot) == FLY_RESULT_OK,
          "new app provides a catalog snapshot");
    check(snapshot != nullptr, "catalog snapshot handle is non-null");

    std::uint64_t generation = 99;
    std::uint64_t count = 99;
    check(fly_catalog_snapshot_generation(nullptr, &generation) == FLY_RESULT_INVALID_ARGUMENT,
          "generation rejects a null snapshot");
    check(generation == 99, "failed generation query preserves output");
    check(fly_catalog_snapshot_generation(snapshot, nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "generation rejects a null output pointer");
    check(fly_catalog_snapshot_count(nullptr, &count) == FLY_RESULT_INVALID_ARGUMENT,
          "count rejects a null snapshot");
    check(count == 99, "failed count query preserves output");
    check(fly_catalog_snapshot_count(snapshot, nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "count rejects a null output pointer");

    check(fly_catalog_snapshot_generation(snapshot, &generation) == FLY_RESULT_OK,
          "generation query succeeds");
    check(generation == 0, "new app snapshot generation is zero");
    check(fly_catalog_snapshot_count(snapshot, &count) == FLY_RESULT_OK,
          "count query succeeds");
    check(count == 0, "new app snapshot is empty");

    std::uint64_t user_count = 99;
    check(fly_catalog_snapshot_user_count(nullptr, &user_count) == FLY_RESULT_INVALID_ARGUMENT,
          "snapshot user count rejects a null snapshot");
    check(user_count == 99, "failed snapshot user count preserves output");
    check(fly_catalog_snapshot_user_count(snapshot, nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "snapshot user count rejects a null output pointer");
    check(fly_catalog_snapshot_user_count(snapshot, &user_count) == FLY_RESULT_OK,
          "snapshot user count succeeds");
    check(user_count == 0, "new app snapshot has no user rows");

    fly_catalog_user_state snapshot_user{};
    snapshot_user.struct_size = FLY_CATALOG_USER_STATE_V1_SIZE;
    snapshot_user.version = FLY_CATALOG_USER_STATE_VERSION_1;
    snapshot_user.favorite = 7u;
    std::uint32_t canonical_required = 77u;
    char snapshot_canonical[8];
    std::memset(snapshot_canonical, 0x44, sizeof(snapshot_canonical));
    check(fly_catalog_snapshot_user_get(nullptr, 0u, snapshot_canonical,
                                        static_cast<std::uint32_t>(sizeof(snapshot_canonical)),
                                        &canonical_required, &snapshot_user) ==
              FLY_RESULT_INVALID_ARGUMENT,
          "snapshot user get rejects a null snapshot");
    check(snapshot_user.favorite == 7u && canonical_required == 77u,
          "invalid snapshot user get preserves outputs");
    check(fly_catalog_snapshot_user_get(snapshot, 0u, snapshot_canonical,
                                        static_cast<std::uint32_t>(sizeof(snapshot_canonical)),
                                        nullptr, &snapshot_user) == FLY_RESULT_INVALID_ARGUMENT,
          "snapshot user get rejects a null required pointer");
    check(fly_catalog_snapshot_user_get(snapshot, 0u, snapshot_canonical,
                                        static_cast<std::uint32_t>(sizeof(snapshot_canonical)),
                                        &canonical_required, nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "snapshot user get rejects a null state pointer");
    check(fly_catalog_snapshot_user_get(snapshot, 0u, nullptr, 1u,
                                        &canonical_required, &snapshot_user) ==
              FLY_RESULT_INVALID_ARGUMENT,
          "snapshot user get rejects a null non-empty buffer");

    fly_catalog_user_state short_snapshot_user = snapshot_user;
    short_snapshot_user.struct_size = FLY_CATALOG_USER_STATE_V1_SIZE - 1u;
    check(fly_catalog_snapshot_user_get(snapshot, 0u, snapshot_canonical,
                                        static_cast<std::uint32_t>(sizeof(snapshot_canonical)),
                                        &canonical_required, &short_snapshot_user) ==
              FLY_RESULT_STRUCT_TOO_SMALL,
          "snapshot user get rejects a short state output");
    check(short_snapshot_user.favorite == 7u && canonical_required == 77u,
          "short snapshot user state preserves outputs");

    fly_catalog_user_state future_snapshot_user = snapshot_user;
    future_snapshot_user.version = FLY_CATALOG_USER_STATE_VERSION_1 + 1u;
    check(fly_catalog_snapshot_user_get(snapshot, 0u, snapshot_canonical,
                                        static_cast<std::uint32_t>(sizeof(snapshot_canonical)),
                                        &canonical_required, &future_snapshot_user) ==
              FLY_RESULT_UNSUPPORTED_VERSION,
          "snapshot user get rejects an unknown state version");
    check(future_snapshot_user.favorite == 7u && canonical_required == 77u,
          "unknown snapshot user version preserves outputs");

    check(fly_catalog_snapshot_user_get(snapshot, 0u, snapshot_canonical,
                                        static_cast<std::uint32_t>(sizeof(snapshot_canonical)),
                                        &canonical_required, &snapshot_user) ==
              FLY_RESULT_OUT_OF_RANGE,
          "snapshot user get rejects an out-of-range index");
    check(snapshot_user.favorite == 7u && canonical_required == 77u,
          "out-of-range snapshot user get preserves outputs");

    char canonical_id[8];
    char variant_id[8];
    char display_name[8];
    char relative_path[8];
    std::memset(canonical_id, 0x30, sizeof(canonical_id));
    std::memset(variant_id, 0x31, sizeof(variant_id));
    std::memset(display_name, 0x32, sizeof(display_name));
    std::memset(relative_path, 0x33, sizeof(relative_path));

    fly_catalog_entry entry;
    std::memset(&entry, 0xA5, sizeof(entry));
    entry.struct_size = FLY_CATALOG_ENTRY_V1_SIZE;
    entry.version = FLY_CATALOG_ENTRY_VERSION_1;
    entry.canonical_id_utf8 = canonical_id;
    entry.canonical_id_capacity = static_cast<std::uint32_t>(sizeof(canonical_id));
    entry.variant_id_utf8 = variant_id;
    entry.variant_id_capacity = static_cast<std::uint32_t>(sizeof(variant_id));
    entry.display_name_utf8 = display_name;
    entry.display_name_capacity = static_cast<std::uint32_t>(sizeof(display_name));
    entry.source_relative_path_utf8 = relative_path;
    entry.source_relative_path_capacity = static_cast<std::uint32_t>(sizeof(relative_path));

    const fly_catalog_entry entry_before = entry;
    char canonical_id_before[sizeof(canonical_id)];
    char variant_id_before[sizeof(variant_id)];
    char display_name_before[sizeof(display_name)];
    char relative_path_before[sizeof(relative_path)];
    std::memcpy(canonical_id_before, canonical_id, sizeof(canonical_id));
    std::memcpy(variant_id_before, variant_id, sizeof(variant_id));
    std::memcpy(display_name_before, display_name, sizeof(display_name));
    std::memcpy(relative_path_before, relative_path, sizeof(relative_path));

    fly_catalog_entry short_entry = entry;
    short_entry.struct_size = FLY_CATALOG_ENTRY_V1_SIZE - 1u;
    const fly_catalog_entry short_entry_before = short_entry;
    check(fly_catalog_snapshot_get(snapshot, 0, &short_entry) == FLY_RESULT_STRUCT_TOO_SMALL,
          "get rejects a short catalog-entry output");
    check(std::memcmp(&short_entry, &short_entry_before, sizeof(short_entry)) == 0,
          "short-entry rejection does not modify the output");

    fly_catalog_entry future_entry = entry;
    future_entry.version = FLY_CATALOG_ENTRY_VERSION_1 + 1u;
    const fly_catalog_entry future_entry_before = future_entry;
    check(fly_catalog_snapshot_get(snapshot, 0, &future_entry) ==
              FLY_RESULT_UNSUPPORTED_VERSION,
          "get rejects an unknown catalog-entry version");
    check(std::memcmp(&future_entry, &future_entry_before, sizeof(future_entry)) == 0,
          "version rejection does not modify the output");

    check(fly_catalog_snapshot_get(snapshot, 0, &entry) == FLY_RESULT_OUT_OF_RANGE,
          "get on an empty snapshot returns out of range");
    check(std::memcmp(&entry, &entry_before, sizeof(entry)) == 0,
          "out-of-range get does not write the entry");
    check(std::memcmp(canonical_id, canonical_id_before, sizeof(canonical_id)) == 0,
          "out-of-range get does not write the canonical-id buffer");
    check(std::memcmp(variant_id, variant_id_before, sizeof(variant_id)) == 0,
          "out-of-range get does not write the variant-id buffer");
    check(std::memcmp(display_name, display_name_before, sizeof(display_name)) == 0,
          "out-of-range get does not write the display-name buffer");
    check(std::memcmp(relative_path, relative_path_before, sizeof(relative_path)) == 0,
          "out-of-range get does not write the relative-path buffer");
    check(fly_catalog_snapshot_get(nullptr, 0, &entry) == FLY_RESULT_INVALID_ARGUMENT,
          "get rejects a null snapshot");
    check(fly_catalog_snapshot_get(snapshot, 0, nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "get rejects a null entry output");

    fly_app_destroy(app);
    app = nullptr;

    generation = 99;
    count = 99;
    check(fly_catalog_snapshot_generation(snapshot, &generation) == FLY_RESULT_OK,
          "snapshot generation remains readable after app destruction");
    check(generation == 0, "retained snapshot keeps generation after app destruction");
    check(fly_catalog_snapshot_count(snapshot, &count) == FLY_RESULT_OK,
          "snapshot count remains readable after app destruction");
    check(count == 0, "retained snapshot stays empty after app destruction");

    fly_catalog_snapshot_release(snapshot);
    fly_catalog_snapshot_release(nullptr);
    fly_app_destroy(nullptr);

    if (failures == 0)
    {
        std::puts("flynes_app_contract_test: PASS");
    }
    return failures == 0 ? 0 : 1;
}
