#include <flynes/flynes_app.h>

#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace {

struct CatalogData final
{
    std::uint64_t generation = 0;
    std::uint64_t entry_count = 0;
};

bool is_utf8_continuation(std::uint8_t byte)
{
    return byte >= 0x80u && byte <= 0xBFu;
}

bool is_valid_root_utf8(const char* bytes, std::uint32_t length)
{
    if (bytes == nullptr || length == 0 || length > FLY_APP_ROOT_MAX_UTF8_BYTES)
    {
        return false;
    }

    std::uint32_t index = 0;
    while (index < length)
    {
        const auto first = static_cast<std::uint8_t>(bytes[index]);
        if (first <= 0x7Fu)
        {
            if (first == 0)
            {
                return false;
            }
            ++index;
            continue;
        }

        if (first >= 0xC2u && first <= 0xDFu)
        {
            if (length - index < 2u ||
                !is_utf8_continuation(static_cast<std::uint8_t>(bytes[index + 1u])))
            {
                return false;
            }
            index += 2u;
            continue;
        }

        if (first >= 0xE0u && first <= 0xEFu)
        {
            if (length - index < 3u)
            {
                return false;
            }
            const auto second = static_cast<std::uint8_t>(bytes[index + 1u]);
            const auto third = static_cast<std::uint8_t>(bytes[index + 2u]);
            const bool valid_second =
                is_utf8_continuation(second) &&
                (first != 0xE0u || second >= 0xA0u) &&
                (first != 0xEDu || second <= 0x9Fu);
            if (!valid_second || !is_utf8_continuation(third))
            {
                return false;
            }
            index += 3u;
            continue;
        }

        if (first >= 0xF0u && first <= 0xF4u)
        {
            if (length - index < 4u)
            {
                return false;
            }
            const auto second = static_cast<std::uint8_t>(bytes[index + 1u]);
            const auto third = static_cast<std::uint8_t>(bytes[index + 2u]);
            const auto fourth = static_cast<std::uint8_t>(bytes[index + 3u]);
            const bool valid_second =
                is_utf8_continuation(second) &&
                (first != 0xF0u || second >= 0x90u) &&
                (first != 0xF4u || second <= 0x8Fu);
            if (!valid_second || !is_utf8_continuation(third) ||
                !is_utf8_continuation(fourth))
            {
                return false;
            }
            index += 4u;
            continue;
        }

        return false;
    }

    return true;
}

fly_result validate_capabilities(const fly_platform_capabilities* capabilities)
{
    if (capabilities == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (capabilities->struct_size < FLY_PLATFORM_CAPABILITIES_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (capabilities->version != FLY_PLATFORM_CAPABILITIES_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    return FLY_RESULT_OK;
}

fly_result validate_config(const fly_app_config* config)
{
    if (config == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (config->struct_size < FLY_APP_CONFIG_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (config->version != FLY_APP_CONFIG_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (!is_valid_root_utf8(config->data_root_utf8, config->data_root_utf8_length) ||
        !is_valid_root_utf8(config->cache_root_utf8, config->cache_root_utf8_length))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return validate_capabilities(config->platform_capabilities);
}

} // namespace

struct fly_app_handle final
{
    fly_app_handle(const fly_app_config& config, std::shared_ptr<const CatalogData> initial_catalog)
        : data_root(config.data_root_utf8, config.data_root_utf8_length),
          cache_root(config.cache_root_utf8, config.cache_root_utf8_length),
          platform_flags(config.platform_capabilities->flags),
          catalog(std::move(initial_catalog))
    {
    }

    std::string data_root;
    std::string cache_root;
    std::uint64_t platform_flags;
    std::shared_ptr<const CatalogData> catalog;
};

struct fly_catalog_snapshot_handle final
{
    explicit fly_catalog_snapshot_handle(std::shared_ptr<const CatalogData> catalog_data)
        : catalog(std::move(catalog_data))
    {
    }

    std::shared_ptr<const CatalogData> catalog;
};

extern "C" fly_result fly_app_create(const fly_app_config* config, fly_app_t** app_out)
{
    if (app_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    *app_out = nullptr;

    const fly_result validation_result = validate_config(config);
    if (validation_result != FLY_RESULT_OK)
    {
        return validation_result;
    }

    try
    {
        std::shared_ptr<const CatalogData> catalog = std::make_shared<CatalogData>();
        std::unique_ptr<fly_app_t> app = std::make_unique<fly_app_t>(*config, std::move(catalog));
        *app_out = app.release();
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_RESULT_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" void fly_app_destroy(fly_app_t* app)
{
    delete app;
}

extern "C" fly_result fly_catalog_snapshot(const fly_app_t* app,
                                             fly_catalog_snapshot_t** snapshot_out)
{
    if (snapshot_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    *snapshot_out = nullptr;
    if (app == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }

    try
    {
        std::unique_ptr<fly_catalog_snapshot_t> snapshot =
            std::make_unique<fly_catalog_snapshot_t>(app->catalog);
        *snapshot_out = snapshot.release();
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_RESULT_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" fly_result fly_catalog_snapshot_generation(
    const fly_catalog_snapshot_t* snapshot,
    std::uint64_t* generation_out)
{
    if (snapshot == nullptr || generation_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    *generation_out = snapshot->catalog->generation;
    return FLY_RESULT_OK;
}

extern "C" fly_result fly_catalog_snapshot_count(const fly_catalog_snapshot_t* snapshot,
                                                   std::uint64_t* count_out)
{
    if (snapshot == nullptr || count_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    *count_out = snapshot->catalog->entry_count;
    return FLY_RESULT_OK;
}

extern "C" fly_result fly_catalog_snapshot_get(const fly_catalog_snapshot_t* snapshot,
                                                 std::uint64_t index,
                                                 fly_catalog_entry* entry_out)
{
    if (snapshot == nullptr || entry_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (entry_out->struct_size < FLY_CATALOG_ENTRY_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (entry_out->version != FLY_CATALOG_ENTRY_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (index >= snapshot->catalog->entry_count)
    {
        return FLY_RESULT_OUT_OF_RANGE;
    }

    return FLY_RESULT_INTERNAL_ERROR;
}

extern "C" void fly_catalog_snapshot_release(fly_catalog_snapshot_t* snapshot)
{
    delete snapshot;
}
