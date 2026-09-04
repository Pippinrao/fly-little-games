#include <flynes/flynes_app.h>

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
static_assert(FLY_PLATFORM_CAPABILITIES_VERSION_1 == 1,
              "capabilities version value changed");
static_assert(FLY_APP_CONFIG_VERSION_1 == 1, "config version value changed");
static_assert(FLY_CATALOG_ENTRY_VERSION_1 == 1, "entry version value changed");

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
static_assert(sizeof(fly_app_config) == 2 * sizeof(std::uint32_t) + 3 * sizeof(const void*),
              "config v1 layout changed");
static_assert(FLY_APP_CONFIG_V1_SIZE == sizeof(fly_app_config),
              "config v1 prefix size changed");

static_assert(offsetof(fly_catalog_entry, struct_size) == 0, "entry prefix changed");
static_assert(offsetof(fly_catalog_entry, version) == sizeof(std::uint32_t),
              "entry version offset changed");
static_assert(offsetof(fly_catalog_entry, entry_id) == 2 * sizeof(std::uint32_t),
              "entry identifier offset changed");
static_assert(offsetof(fly_catalog_entry, content_size) ==
                  2 * sizeof(std::uint32_t) + sizeof(std::uint64_t),
              "entry content-size offset changed");
static_assert(offsetof(fly_catalog_entry, flags) == 24, "entry flags offset changed");
static_assert(offsetof(fly_catalog_entry, reserved) == 28, "entry reserved offset changed");
static_assert(offsetof(fly_catalog_entry, display_name_utf8) == 32,
              "entry display-name pointer offset changed");
static_assert(offsetof(fly_catalog_entry, display_name_capacity) == 32 + sizeof(char*),
              "entry display-name capacity offset changed");
static_assert(offsetof(fly_catalog_entry, display_name_required) == 36 + sizeof(char*),
              "entry display-name required-size offset changed");
static_assert(offsetof(fly_catalog_entry, source_uri_utf8) == 40 + sizeof(char*),
              "entry source-URI pointer offset changed");
static_assert(offsetof(fly_catalog_entry, source_uri_capacity) == 40 + 2 * sizeof(char*),
              "entry source-URI capacity offset changed");
static_assert(offsetof(fly_catalog_entry, source_uri_required) == 44 + 2 * sizeof(char*),
              "entry source-URI required-size offset changed");
static_assert(sizeof(fly_catalog_entry) == 48 + 2 * sizeof(char*),
              "catalog entry v1 layout changed");
static_assert(FLY_CATALOG_ENTRY_V1_SIZE == sizeof(fly_catalog_entry),
              "catalog entry v1 prefix size changed");

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
                        "create rejects a null data root");
    bad_config.data_root_utf8 = "";
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects an empty data root");

    bad_config = config;
    bad_config.cache_root_utf8 = nullptr;
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects a null cache root");
    bad_config.cache_root_utf8 = "";
    check_failed_create(&bad_config, FLY_RESULT_INVALID_ARGUMENT,
                        "create rejects an empty cache root");

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

    char display_name[8];
    char source_uri[8];
    std::memset(display_name, 0x31, sizeof(display_name));
    std::memset(source_uri, 0x32, sizeof(source_uri));

    fly_catalog_entry entry;
    std::memset(&entry, 0xA5, sizeof(entry));
    entry.struct_size = FLY_CATALOG_ENTRY_V1_SIZE;
    entry.version = FLY_CATALOG_ENTRY_VERSION_1;
    entry.display_name_utf8 = display_name;
    entry.display_name_capacity = static_cast<std::uint32_t>(sizeof(display_name));
    entry.source_uri_utf8 = source_uri;
    entry.source_uri_capacity = static_cast<std::uint32_t>(sizeof(source_uri));

    const fly_catalog_entry entry_before = entry;
    char display_name_before[sizeof(display_name)];
    char source_uri_before[sizeof(source_uri)];
    std::memcpy(display_name_before, display_name, sizeof(display_name));
    std::memcpy(source_uri_before, source_uri, sizeof(source_uri));

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
    check(std::memcmp(display_name, display_name_before, sizeof(display_name)) == 0,
          "out-of-range get does not write the display-name buffer");
    check(std::memcmp(source_uri, source_uri_before, sizeof(source_uri)) == 0,
          "out-of-range get does not write the source-URI buffer");
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
