#include <flynes/flynes_app.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

_Static_assert(sizeof(fly_result) == sizeof(int32_t), "fly_result must be 32-bit");
_Static_assert(offsetof(fly_app_config, struct_size) == 0, "config prefix changed");
_Static_assert(offsetof(fly_app_config, version) == sizeof(uint32_t),
               "config version offset changed");
_Static_assert(offsetof(fly_catalog_entry, struct_size) == 0, "entry prefix changed");
_Static_assert(offsetof(fly_catalog_entry, version) == sizeof(uint32_t),
               "entry version offset changed");

int main(void)
{
    fly_platform_capabilities capabilities = {0};
    fly_app_config config = {0};
    fly_app_t* app = NULL;
    fly_catalog_snapshot_t* snapshot = NULL;
    uint64_t generation = UINT64_MAX;
    uint64_t count = UINT64_MAX;

    capabilities.struct_size = FLY_PLATFORM_CAPABILITIES_V1_SIZE;
    capabilities.version = FLY_PLATFORM_CAPABILITIES_VERSION_1;

    config.struct_size = FLY_APP_CONFIG_V1_SIZE;
    config.version = FLY_APP_CONFIG_VERSION_1;
    config.data_root_utf8 = "c-test-data";
    config.cache_root_utf8 = "c-test-cache";
    config.platform_capabilities = &capabilities;
    config.data_root_utf8_length = (uint32_t)(sizeof("c-test-data") - 1u);
    config.cache_root_utf8_length = (uint32_t)(sizeof("c-test-cache") - 1u);

    if (fly_app_create(&config, &app) != FLY_RESULT_OK || app == NULL)
    {
        fputs("C consumer could not create app\n", stderr);
        return 1;
    }
    if (fly_catalog_snapshot(app, &snapshot) != FLY_RESULT_OK || snapshot == NULL)
    {
        fputs("C consumer could not acquire snapshot\n", stderr);
        fly_app_destroy(app);
        return 1;
    }

    fly_app_destroy(app);

    if (fly_catalog_snapshot_generation(snapshot, &generation) != FLY_RESULT_OK ||
        generation != UINT64_C(0))
    {
        fputs("C consumer observed wrong generation\n", stderr);
        fly_catalog_snapshot_release(snapshot);
        return 1;
    }
    if (fly_catalog_snapshot_count(snapshot, &count) != FLY_RESULT_OK || count != UINT64_C(0))
    {
        fputs("C consumer observed wrong count\n", stderr);
        fly_catalog_snapshot_release(snapshot);
        return 1;
    }

    fly_catalog_snapshot_release(snapshot);
    fly_catalog_snapshot_release(NULL);
    fly_app_destroy(NULL);
    puts("flynes_c_header_test: PASS");
    return 0;
}
