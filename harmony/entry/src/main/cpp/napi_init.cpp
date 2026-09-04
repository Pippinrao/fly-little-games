#include "catalog_smoke.hpp"

#include <flynes/flynes_app.h>

#include "napi/native_api.h"

#include <cstddef>
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class NapiCallError final : public std::runtime_error
{
public:
    NapiCallError(const char* step, napi_status status)
        : std::runtime_error(std::string(step) + " failed: napi_status=" +
                             std::to_string(static_cast<int>(status)))
    {
    }
};

class NapiTypeError final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void require_napi(napi_status status, const char* step)
{
    if (status != napi_ok)
    {
        throw NapiCallError(step, status);
    }
}

std::string read_root(napi_env env, napi_value value, const char* argument_name)
{
    napi_valuetype type = napi_undefined;
    require_napi(napi_typeof(env, value, &type), "inspect catalogSmoke argument");
    if (type != napi_string)
    {
        throw NapiTypeError(std::string(argument_name) + " must be a string");
    }

    std::size_t byte_count = 0;
    require_napi(napi_get_value_string_utf8(env, value, nullptr, 0, &byte_count),
                 "measure catalogSmoke root");
    if (byte_count == 0 || byte_count > FLY_APP_ROOT_MAX_UTF8_BYTES)
    {
        throw NapiTypeError(std::string(argument_name) +
                            " must contain 1..4096 UTF-8 bytes");
    }

    std::vector<char> bytes(byte_count + 1u, '\0');
    std::size_t bytes_written = 0;
    require_napi(napi_get_value_string_utf8(
                     env, value, bytes.data(), bytes.size(), &bytes_written),
                 "read catalogSmoke root");
    if (bytes_written != byte_count)
    {
        throw NapiCallError("read catalogSmoke root invariant", napi_generic_failure);
    }
    return std::string(bytes.data(), bytes_written);
}

napi_value create_string(napi_env env, const std::string& value, const char* step)
{
    napi_value result = nullptr;
    require_napi(napi_create_string_utf8(env, value.data(), value.size(), &result), step);
    return result;
}

napi_value make_result(napi_env env, const flynes::harmony::CatalogSmokeResult& smoke)
{
    napi_value result = nullptr;
    require_napi(napi_create_object(env, &result), "create catalogSmoke result");

    napi_value generation = create_string(env, smoke.generation, "create generation string");
    require_napi(napi_set_named_property(env, result, "generation", generation),
                 "set generation result");

    napi_value count = create_string(env, smoke.count, "create count string");
    require_napi(napi_set_named_property(env, result, "count", count), "set count result");
    return result;
}

napi_value report_error(napi_env env, const char* message, bool type_error) noexcept
{
    bool exception_pending = false;
    const napi_status pending_status = napi_is_exception_pending(env, &exception_pending);
    if (pending_status != napi_ok || exception_pending)
    {
        return nullptr;
    }

    const napi_status throw_status = type_error
                                         ? napi_throw_type_error(env, nullptr, message)
                                         : napi_throw_error(env, nullptr, message);
    if (throw_status != napi_ok)
    {
        return nullptr;
    }
    return nullptr;
}

napi_value CatalogSmoke(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 2;
        napi_value arguments[2] = {nullptr, nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read catalogSmoke arguments");
        if (argument_count < 2)
        {
            throw NapiTypeError("catalogSmoke requires dataRoot and cacheRoot strings");
        }

        const std::string data_root = read_root(env, arguments[0], "dataRoot");
        const std::string cache_root = read_root(env, arguments[1], "cacheRoot");
        const flynes::harmony::CatalogSmokeResult result =
            flynes::harmony::run_catalog_smoke(data_root, cache_root);
        return make_result(env, result);
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::bad_alloc&)
    {
        return report_error(env, "catalogSmoke failed: native allocation error", false);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "catalogSmoke failed: unknown native error", false);
    }
}

napi_value Init(napi_env env, napi_value exports)
{
    try
    {
        const napi_property_descriptor descriptors[] = {
            {"catalogSmoke", nullptr, CatalogSmoke, nullptr, nullptr, nullptr, napi_default, nullptr},
        };
        require_napi(napi_define_properties(
                         env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors),
                     "define entry exports");
        return exports;
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "entry initialization failed", false);
    }
}

static napi_module entry_module = {
    1,
    0,
    nullptr,
    Init,
    "entry",
    nullptr,
    {nullptr, nullptr, nullptr, nullptr},
};

} // namespace

extern "C" __attribute__((constructor)) void RegisterEntryModule()
{
    napi_module_register(&entry_module);
}
