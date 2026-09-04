#include "catalog_smoke.hpp"

#include <flynes/flynes_app.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

namespace flynes::harmony {
namespace {

class CatalogSmokeError final : public std::runtime_error
{
public:
    CatalogSmokeError(const char* step, fly_result result)
        : std::runtime_error(std::string(step) + " failed: fly_result=" +
                             std::to_string(static_cast<int>(result)))
    {
    }
};

struct AppDeleter final
{
    void operator()(fly_app_t* app) const noexcept
    {
        fly_app_destroy(app);
    }
};

struct SnapshotDeleter final
{
    void operator()(fly_catalog_snapshot_t* snapshot) const noexcept
    {
        fly_catalog_snapshot_release(snapshot);
    }
};

using AppHandle = std::unique_ptr<fly_app_t, AppDeleter>;
using SnapshotHandle = std::unique_ptr<fly_catalog_snapshot_t, SnapshotDeleter>;

void validate_root_size(std::string_view root, const char* step)
{
    if (root.empty() || root.size() > FLY_APP_ROOT_MAX_UTF8_BYTES)
    {
        throw CatalogSmokeError(step, FLY_RESULT_INVALID_ARGUMENT);
    }
}

void require_ok(fly_result result, const char* step)
{
    if (result != FLY_RESULT_OK)
    {
        throw CatalogSmokeError(step, result);
    }
}

std::string decimal_string(std::uint64_t value)
{
    std::array<char, 32> buffer{};
    const auto conversion = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (conversion.ec != std::errc{})
    {
        throw CatalogSmokeError("format catalog value", FLY_RESULT_INTERNAL_ERROR);
    }
    return std::string(buffer.data(), conversion.ptr);
}

} // namespace

CatalogSmokeResult run_catalog_smoke(std::string_view data_root, std::string_view cache_root)
{
    validate_root_size(data_root, "validate data root");
    validate_root_size(cache_root, "validate cache root");

    const fly_platform_capabilities capabilities{
        FLY_PLATFORM_CAPABILITIES_V1_SIZE,
        FLY_PLATFORM_CAPABILITIES_VERSION_1,
        UINT64_C(0),
    };
    const fly_app_config config{
        FLY_APP_CONFIG_V1_SIZE,
        FLY_APP_CONFIG_VERSION_1,
        data_root.data(),
        cache_root.data(),
        &capabilities,
        static_cast<std::uint32_t>(data_root.size()),
        static_cast<std::uint32_t>(cache_root.size()),
    };

    fly_app_t* raw_app = nullptr;
    require_ok(fly_app_create(&config, &raw_app), "fly_app_create");
    if (raw_app == nullptr)
    {
        throw CatalogSmokeError("fly_app_create invariant", FLY_RESULT_INTERNAL_ERROR);
    }
    AppHandle app(raw_app);

    fly_catalog_snapshot_t* raw_snapshot = nullptr;
    require_ok(fly_catalog_snapshot(app.get(), &raw_snapshot), "fly_catalog_snapshot");
    if (raw_snapshot == nullptr)
    {
        throw CatalogSmokeError("fly_catalog_snapshot invariant", FLY_RESULT_INTERNAL_ERROR);
    }
    SnapshotHandle snapshot(raw_snapshot);

    // The smoke deliberately destroys the app before reading the immutable snapshot.
    app.reset();

    std::uint64_t generation = 0;
    require_ok(fly_catalog_snapshot_generation(snapshot.get(), &generation),
               "fly_catalog_snapshot_generation");

    std::uint64_t count = 0;
    require_ok(fly_catalog_snapshot_count(snapshot.get(), &count),
               "fly_catalog_snapshot_count");

    CatalogSmokeResult result{decimal_string(generation), decimal_string(count)};
    snapshot.reset();
    return result;
}

} // namespace flynes::harmony
