#include "catalog_smoke.hpp"
#include "play_session.hpp"
#include "product_bridge.hpp"

#include <flynes/flynes_app.h>

#include "napi/native_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
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

std::unique_ptr<flynes::harmony::PlaySession> g_play;

struct AppDeleter final
{
    void operator()(fly_app_t* app) const noexcept
    {
        fly_app_destroy(app);
    }
};

struct ScanDeleter final
{
    void operator()(fly_scan_t* scan) const noexcept
    {
        fly_scan_abort(scan);
    }
};

std::unique_ptr<fly_app_t, AppDeleter> g_app;
std::unique_ptr<fly_scan_t, ScanDeleter> g_scan;

class FlyCallError final : public std::runtime_error
{
public:
    FlyCallError(const char* step, fly_result result)
        : std::runtime_error(std::string(step) + " failed: fly_result=" +
                             std::to_string(static_cast<int>(result)))
    {
    }
};

void require_fly(fly_result result, const char* step)
{
    if (result != FLY_RESULT_OK)
    {
        throw FlyCallError(step, result);
    }
}

fly_app_t& require_app()
{
    if (g_app == nullptr)
    {
        throw NapiTypeError("app is not open");
    }
    return *g_app;
}

int hex_nibble(char value)
{
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f')
    {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F')
    {
        return value - 'A' + 10;
    }
    throw NapiTypeError("source UUID hex must be 32 hexadecimal digits");
}

void parse_uuid_hex(const std::string& hex, std::uint8_t* uuid_out)
{
    if (hex.size() != 32)
    {
        throw NapiTypeError("source UUID hex must be 32 hexadecimal digits");
    }
    bool assigned = false;
    for (std::size_t index = 0; index < 16; ++index)
    {
        const int high = hex_nibble(hex[index * 2u]);
        const int low = hex_nibble(hex[index * 2u + 1u]);
        uuid_out[index] = static_cast<std::uint8_t>((high << 4) | low);
        assigned = assigned || uuid_out[index] != 0;
    }
    if (!assigned)
    {
        throw NapiTypeError("source UUID must not be all zero");
    }
}

std::string uuid_to_hex(const std::uint8_t* uuid)
{
    static const char kDigits[] = "0123456789abcdef";
    std::string hex(32, '0');
    for (std::size_t index = 0; index < 16; ++index)
    {
        hex[index * 2u] = kDigits[(uuid[index] >> 4) & 0x0f];
        hex[index * 2u + 1u] = kDigits[uuid[index] & 0x0f];
    }
    return hex;
}

void open_app(const std::string& data_root, const std::string& cache_root)
{
    if (g_app != nullptr)
    {
        return;
    }
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
    g_scan.reset();
    g_app.reset();
    fly_app_t* raw_app = nullptr;
    require_fly(fly_app_create(&config, &raw_app), "fly_app_create");
    if (raw_app == nullptr)
    {
        throw FlyCallError("fly_app_create invariant", FLY_RESULT_INTERNAL_ERROR);
    }
    g_app.reset(raw_app);
}

std::vector<std::uint8_t> read_buffer(napi_env env, napi_value value, const char* argument_name)
{
    bool is_typedarray = false;
    require_napi(napi_is_typedarray(env, value, &is_typedarray), "inspect typedarray");
    if (is_typedarray)
    {
        napi_typedarray_type type = napi_uint8_array;
        std::size_t element_count = 0;
        void* data = nullptr;
        napi_value arraybuffer = nullptr;
        std::size_t offset = 0;
        require_napi(napi_get_typedarray_info(
                         env, value, &type, &element_count, &data, &arraybuffer, &offset),
                     "read typedarray");
        (void)arraybuffer;
        (void)offset;
        if (type != napi_uint8_array)
        {
            throw NapiTypeError(std::string(argument_name) + " must be a Uint8Array");
        }
        if (data == nullptr || element_count == 0)
        {
            throw NapiTypeError(std::string(argument_name) + " must not be empty");
        }
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        return std::vector<std::uint8_t>(bytes, bytes + element_count);
    }

    bool is_arraybuffer = false;
    require_napi(napi_is_arraybuffer(env, value, &is_arraybuffer), "inspect arraybuffer");
    if (!is_arraybuffer)
    {
        throw NapiTypeError(std::string(argument_name) +
                            " must be a Uint8Array or ArrayBuffer");
    }
    void* data = nullptr;
    std::size_t byte_count = 0;
    require_napi(napi_get_arraybuffer_info(env, value, &data, &byte_count), "read arraybuffer");
    if (data == nullptr || byte_count == 0)
    {
        throw NapiTypeError(std::string(argument_name) + " must not be empty");
    }
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    return std::vector<std::uint8_t>(bytes, bytes + byte_count);
}

napi_value create_arraybuffer(napi_env env, const void* bytes, std::size_t size, const char* step)
{
    void* data = nullptr;
    napi_value buffer = nullptr;
    require_napi(napi_create_arraybuffer(env, size, &data, &buffer), step);
    if (size != 0)
    {
        if (bytes == nullptr || data == nullptr)
        {
            throw NapiCallError(step, napi_generic_failure);
        }
        std::memcpy(data, bytes, size);
    }
    return buffer;
}

napi_value create_uint32(napi_env env, std::uint32_t value, const char* step)
{
    napi_value result = nullptr;
    require_napi(napi_create_uint32(env, value, &result), step);
    return result;
}

napi_value create_int64(napi_env env, std::int64_t value, const char* step)
{
    napi_value result = nullptr;
    require_napi(napi_create_int64(env, value, &result), step);
    return result;
}

napi_value create_double(napi_env env, double value, const char* step)
{
    napi_value result = nullptr;
    require_napi(napi_create_double(env, value, &result), step);
    return result;
}

napi_value create_bool(napi_env env, bool value, const char* step)
{
    napi_value result = nullptr;
    require_napi(napi_get_boolean(env, value, &result), step);
    return result;
}

std::string read_utf8_string(napi_env env, napi_value value, const char* argument_name)
{
    napi_valuetype type = napi_undefined;
    require_napi(napi_typeof(env, value, &type), "inspect string argument");
    if (type != napi_string)
    {
        throw NapiTypeError(std::string(argument_name) + " must be a string");
    }

    std::size_t byte_count = 0;
    require_napi(napi_get_value_string_utf8(env, value, nullptr, 0, &byte_count),
                 "measure string argument");
    std::vector<char> bytes(byte_count + 1u, '\0');
    std::size_t bytes_written = 0;
    require_napi(napi_get_value_string_utf8(
                     env, value, bytes.data(), bytes.size(), &bytes_written),
                 "read string argument");
    if (bytes_written != byte_count)
    {
        throw NapiCallError("read string argument invariant", napi_generic_failure);
    }
    return std::string(bytes.data(), bytes_written);
}

napi_value named_property(napi_env env, napi_value object, const char* name)
{
    napi_value value = nullptr;
    require_napi(napi_get_named_property(env, object, name, &value), "read named property");
    return value;
}

std::int32_t read_int32(napi_env env, napi_value value, const char* argument_name)
{
    napi_valuetype type = napi_undefined;
    require_napi(napi_typeof(env, value, &type), "inspect int32 argument");
    if (type != napi_number)
    {
        throw NapiTypeError(std::string(argument_name) + " must be a number");
    }
    std::int32_t result = 0;
    require_napi(napi_get_value_int32(env, value, &result), "read int32 argument");
    return result;
}

std::int64_t read_int64(napi_env env, napi_value value, const char* argument_name)
{
    napi_valuetype type = napi_undefined;
    require_napi(napi_typeof(env, value, &type), "inspect int64 argument");
    if (type != napi_number)
    {
        throw NapiTypeError(std::string(argument_name) + " must be a number");
    }
    std::int64_t result = 0;
    require_napi(napi_get_value_int64(env, value, &result), "read int64 argument");
    return result;
}

double read_double(napi_env env, napi_value value, const char* argument_name)
{
    napi_valuetype type = napi_undefined;
    require_napi(napi_typeof(env, value, &type), "inspect double argument");
    if (type != napi_number)
    {
        throw NapiTypeError(std::string(argument_name) + " must be a number");
    }
    double result = 0;
    require_napi(napi_get_value_double(env, value, &result), "read double argument");
    return result;
}

bool read_bool(napi_env env, napi_value value, const char* argument_name)
{
    napi_valuetype type = napi_undefined;
    require_napi(napi_typeof(env, value, &type), "inspect bool argument");
    if (type != napi_boolean)
    {
        throw NapiTypeError(std::string(argument_name) + " must be a boolean");
    }
    bool result = false;
    require_napi(napi_get_value_bool(env, value, &result), "read bool argument");
    return result;
}

flynes::harmony::GameCenterRow read_game_center_row(napi_env env, napi_value value)
{
    napi_valuetype type = napi_undefined;
    require_napi(napi_typeof(env, value, &type), "inspect game center row");
    if (type != napi_object)
    {
        throw NapiTypeError("gameCenterFilter row must be an object");
    }

    flynes::harmony::GameCenterRow row;
    row.canonical_id =
        read_utf8_string(env, named_property(env, value, "canonicalId"), "canonicalId");
    row.title_en = read_utf8_string(env, named_property(env, value, "titleEn"), "titleEn");
    row.title_zh_hans =
        read_utf8_string(env, named_property(env, value, "titleZhHans"), "titleZhHans");
    row.builtin = read_bool(env, named_property(env, value, "builtin"), "builtin");
    row.favorite = read_bool(env, named_property(env, value, "favorite"), "favorite");
    row.last_played_sequence = read_int64(
        env, named_property(env, value, "lastPlayedSequence"), "lastPlayedSequence");
    row.original_filename = read_utf8_string(
        env, named_property(env, value, "originalFilename"), "originalFilename");
    return row;
}

napi_value make_game_center_row(napi_env env, const flynes::harmony::GameCenterRow& row)
{
    napi_value result = nullptr;
    require_napi(napi_create_object(env, &result), "create GameCenterRow");
    require_napi(napi_set_named_property(
                     env, result, "canonicalId",
                     create_string(env, row.canonical_id, "create canonicalId")),
                 "set canonicalId");
    require_napi(napi_set_named_property(
                     env, result, "titleEn", create_string(env, row.title_en, "create titleEn")),
                 "set titleEn");
    require_napi(napi_set_named_property(
                     env, result, "titleZhHans",
                     create_string(env, row.title_zh_hans, "create titleZhHans")),
                 "set titleZhHans");
    require_napi(napi_set_named_property(
                     env, result, "builtin", create_bool(env, row.builtin, "create builtin")),
                 "set builtin");
    require_napi(napi_set_named_property(
                     env, result, "favorite", create_bool(env, row.favorite, "create favorite")),
                 "set favorite");
    require_napi(napi_set_named_property(
                     env, result, "lastPlayedSequence",
                     create_int64(env, row.last_played_sequence, "create lastPlayedSequence")),
                 "set lastPlayedSequence");
    require_napi(napi_set_named_property(
                     env, result, "originalFilename",
                     create_string(env, row.original_filename, "create originalFilename")),
                 "set originalFilename");
    return result;
}

napi_value make_hit_map(napi_env env, const flynes::harmony::HitMapDto& hit_map)
{
    napi_value result = nullptr;
    require_napi(napi_create_object(env, &result), "create HitMapDto");
    require_napi(napi_set_named_property(
                     env, result, "dpadLeft",
                     create_double(env, hit_map.dpad_left, "create dpadLeft")),
                 "set dpadLeft");
    require_napi(napi_set_named_property(
                     env, result, "dpadTop",
                     create_double(env, hit_map.dpad_top, "create dpadTop")),
                 "set dpadTop");
    require_napi(napi_set_named_property(
                     env, result, "dpadRight",
                     create_double(env, hit_map.dpad_right, "create dpadRight")),
                 "set dpadRight");
    require_napi(napi_set_named_property(
                     env, result, "dpadBottom",
                     create_double(env, hit_map.dpad_bottom, "create dpadBottom")),
                 "set dpadBottom");
    require_napi(napi_set_named_property(
                     env, result, "directionMode",
                     create_int64(env, hit_map.direction_mode, "create directionMode")),
                 "set directionMode");
    require_napi(napi_set_named_property(
                     env, result, "deadZone",
                     create_double(env, hit_map.dead_zone, "create deadZone")),
                 "set deadZone");
    require_napi(napi_set_named_property(
                     env, result, "joystickRadius",
                     create_double(env, hit_map.joystick_radius, "create joystickRadius")),
                 "set joystickRadius");
    require_napi(napi_set_named_property(
                     env, result, "joystickTravelRadius",
                     create_double(env, hit_map.joystick_travel_radius,
                                   "create joystickTravelRadius")),
                 "set joystickTravelRadius");

    napi_value controls = nullptr;
    require_napi(napi_create_array_with_length(
                     env, hit_map.controls.size(), &controls),
                 "create HitMapDto controls");
    for (std::size_t index = 0; index < hit_map.controls.size(); ++index)
    {
        const flynes::harmony::HitMapControlDto& control = hit_map.controls[index];
        napi_value item = nullptr;
        require_napi(napi_create_object(env, &item), "create HitMap control");
        require_napi(napi_set_named_property(
                         env, item, "control",
                         create_string(env, control.control, "create control id")),
                     "set control id");
        require_napi(napi_set_named_property(
                         env, item, "centerX",
                         create_double(env, control.center_x, "create centerX")),
                     "set centerX");
        require_napi(napi_set_named_property(
                         env, item, "centerY",
                         create_double(env, control.center_y, "create centerY")),
                     "set centerY");
        require_napi(napi_set_named_property(
                         env, item, "width", create_double(env, control.width, "create width")),
                     "set control width");
        require_napi(napi_set_named_property(
                         env, item, "height",
                         create_double(env, control.height, "create height")),
                     "set control height");
        require_napi(napi_set_named_property(
                         env, item, "shape", create_string(env, control.shape, "create shape")),
                     "set shape");
        require_napi(napi_set_element(env, controls, static_cast<std::uint32_t>(index), item),
                     "set HitMap control element");
    }
    require_napi(napi_set_named_property(env, result, "controls", controls), "set controls");
    return result;
}

flynes::harmony::PlaySession& require_play()
{
    if (g_play == nullptr)
    {
        throw NapiTypeError("play session is not open");
    }
    return *g_play;
}

napi_value make_play_result(napi_env env, const flynes::harmony::PlayStepResult& step)
{
    napi_value result = nullptr;
    require_napi(napi_create_object(env, &result), "create playStep result");
    require_napi(napi_set_named_property(
                     env, result, "frameIndex",
                     create_int64(env, static_cast<std::int64_t>(step.frame_index),
                                  "create frameIndex")),
                 "set frameIndex");
    require_napi(napi_set_named_property(
                     env, result, "width", create_uint32(env, step.width, "create width")),
                 "set width");
    require_napi(napi_set_named_property(
                     env, result, "height", create_uint32(env, step.height, "create height")),
                 "set height");
    require_napi(napi_set_named_property(
                     env, result, "format", create_uint32(env, step.format, "create format")),
                 "set format");
    require_napi(napi_set_named_property(
                     env, result, "bytesWritten",
                     create_uint32(env, step.bytes_written, "create bytesWritten")),
                 "set bytesWritten");
    require_napi(napi_set_named_property(
                     env, result, "pcmSampleCount",
                     create_uint32(env, step.pcm_sample_count, "create pcmSampleCount")),
                 "set pcmSampleCount");
    require_napi(napi_set_named_property(
                     env, result, "appliedButtons",
                     create_uint32(env, step.applied_buttons, "create appliedButtons")),
                 "set appliedButtons");
    require_napi(napi_set_named_property(
                     env, result, "rgb565",
                     create_arraybuffer(env, step.rgb565.data(), step.rgb565.size(),
                                        "create rgb565")),
                 "set rgb565");
    require_napi(napi_set_named_property(
                     env, result, "pcm",
                     create_arraybuffer(env, step.pcm.data(),
                                        step.pcm.size() * sizeof(std::int16_t), "create pcm")),
                 "set pcm");
    return result;
}

napi_value PlayOpen(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read playOpen arguments");
        if (argument_count < 1)
        {
            throw NapiTypeError("playOpen requires ROM bytes");
        }
        const std::vector<std::uint8_t> rom = read_buffer(env, arguments[0], "rom");
        g_play.reset();
        g_play = flynes::harmony::PlaySession::open(rom.data(), rom.size());
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "playOpen undefined");
        return undefined;
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::bad_alloc&)
    {
        return report_error(env, "playOpen failed: native allocation error", false);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "playOpen failed: unknown native error", false);
    }
}

napi_value PlaySetButtons(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read playSetButtons arguments");
        if (argument_count < 1)
        {
            throw NapiTypeError("playSetButtons requires a button mask");
        }
        std::uint32_t buttons = 0;
        require_napi(napi_get_value_uint32(env, arguments[0], &buttons), "read button mask");
        require_play().set_port0_buttons(buttons);
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "playSetButtons undefined");
        return undefined;
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "playSetButtons failed: unknown native error", false);
    }
}

napi_value PlayStep(napi_env env, napi_callback_info info)
{
    try
    {
        (void)info;
        return make_play_result(env, require_play().step());
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "playStep failed: unknown native error", false);
    }
}

napi_value PlaySaveCheckpoint(napi_env env, napi_callback_info info)
{
    try
    {
        (void)info;
        const std::vector<std::uint8_t> bytes = require_play().save_checkpoint();
        return create_arraybuffer(env, bytes.data(), bytes.size(), "create checkpoint");
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "playSaveCheckpoint failed: unknown native error", false);
    }
}

napi_value PlayClose(napi_env env, napi_callback_info info)
{
    try
    {
        (void)info;
        g_play.reset();
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "playClose undefined");
        return undefined;
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "playClose failed: unknown native error", false);
    }
}

napi_value GameCenterFilter(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 3;
        napi_value arguments[3] = {nullptr, nullptr, nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read gameCenterFilter arguments");
        if (argument_count < 3)
        {
            throw NapiTypeError("gameCenterFilter requires rows, category, and query");
        }

        bool is_array = false;
        require_napi(napi_is_array(env, arguments[0], &is_array), "inspect gameCenterFilter rows");
        if (!is_array)
        {
            throw NapiTypeError("gameCenterFilter rows must be an array");
        }
        std::uint32_t length = 0;
        require_napi(napi_get_array_length(env, arguments[0], &length),
                     "read gameCenterFilter length");
        std::vector<flynes::harmony::GameCenterRow> rows;
        rows.reserve(length);
        for (std::uint32_t index = 0; index < length; ++index)
        {
            napi_value element = nullptr;
            require_napi(napi_get_element(env, arguments[0], index, &element),
                         "read gameCenterFilter row");
            rows.push_back(read_game_center_row(env, element));
        }

        const std::string category = read_utf8_string(env, arguments[1], "category");
        const std::string query = read_utf8_string(env, arguments[2], "query");
        const std::vector<flynes::harmony::GameCenterRow> filtered =
            flynes::harmony::game_center_filter(rows, category, query);

        napi_value result = nullptr;
        require_napi(napi_create_array_with_length(env, filtered.size(), &result),
                     "create gameCenterFilter result");
        for (std::size_t index = 0; index < filtered.size(); ++index)
        {
            require_napi(napi_set_element(env, result, static_cast<std::uint32_t>(index),
                                          make_game_center_row(env, filtered[index])),
                         "set gameCenterFilter result row");
        }
        return result;
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "gameCenterFilter failed: unknown native error", false);
    }
}

napi_value ControlLayoutRecommended(napi_env env, napi_callback_info info)
{
    try
    {
        (void)info;
        return create_string(env, flynes::harmony::control_layout_recommended(),
                             "create recommended layout");
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "controlLayoutRecommended failed: unknown native error", false);
    }
}

napi_value ControlLayoutDecodeOrRecommended(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read controlLayoutDecodeOrRecommended arguments");
        if (argument_count < 1)
        {
            throw NapiTypeError("controlLayoutDecodeOrRecommended requires a layout string");
        }
        const std::string value = read_utf8_string(env, arguments[0], "value");
        return create_string(env,
                             flynes::harmony::control_layout_decode_or_recommended(value),
                             "create decoded layout");
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(
            env, "controlLayoutDecodeOrRecommended failed: unknown native error", false);
    }
}

napi_value HitMapFromLayout(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 10;
        napi_value arguments[10] = {};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read hitMapFromLayout arguments");
        if (argument_count < 10)
        {
            throw NapiTypeError("hitMapFromLayout requires layout geometry arguments");
        }
        const flynes::harmony::HitMapDto hit_map = flynes::harmony::hit_map_from_layout(
            read_int32(env, arguments[0], "width"),
            read_int32(env, arguments[1], "height"),
            static_cast<float>(read_double(env, arguments[2], "density")),
            read_int32(env, arguments[3], "insetL"),
            read_int32(env, arguments[4], "insetR"),
            read_int32(env, arguments[5], "insetT"),
            read_int32(env, arguments[6], "insetB"),
            read_utf8_string(env, arguments[7], "layoutUtf8"),
            read_int32(env, arguments[8], "directionMode"),
            static_cast<float>(read_double(env, arguments[9], "deadZone")));
        return make_hit_map(env, hit_map);
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "hitMapFromLayout failed: unknown native error", false);
    }
}

napi_value PauseCommands(napi_env env, napi_callback_info info)
{
    try
    {
        (void)info;
        const std::vector<std::string> commands = flynes::harmony::pause_commands();
        napi_value result = nullptr;
        require_napi(napi_create_array_with_length(env, commands.size(), &result),
                     "create pauseCommands result");
        for (std::size_t index = 0; index < commands.size(); ++index)
        {
            require_napi(napi_set_element(env, result, static_cast<std::uint32_t>(index),
                                          create_string(env, commands[index], "create pause id")),
                         "set pauseCommands element");
        }
        return result;
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "pauseCommands failed: unknown native error", false);
    }
}

std::string control_layout_get_utf8()
{
    fly_app_t& app = require_app();
    std::uint32_t required = 0;
    const fly_result size_status =
        fly_control_layout_get(&app, nullptr, 0, &required);
    if (size_status != FLY_RESULT_OK && size_status != FLY_RESULT_BUFFER_TOO_SMALL)
    {
        throw FlyCallError("fly_control_layout_get", size_status);
    }
    std::vector<char> bytes(required == 0 ? 1u : required, '\0');
    require_fly(fly_control_layout_get(&app, bytes.data(), static_cast<std::uint32_t>(bytes.size()),
                                       &required),
                "fly_control_layout_get");
    if (required == 0)
    {
        return {};
    }
    const std::size_t length = required > 0 ? static_cast<std::size_t>(required - 1u) : 0;
    return std::string(bytes.data(), length);
}

fly_settings_snapshot read_settings_dto(napi_env env, napi_value value,
                                        std::vector<char>* locale_storage,
                                        std::vector<char>* last_played_storage)
{
    napi_valuetype type = napi_undefined;
    require_napi(napi_typeof(env, value, &type), "inspect settings snapshot");
    if (type != napi_object)
    {
        throw NapiTypeError("settingsApply snapshot must be an object");
    }

    const std::string locale =
        read_utf8_string(env, named_property(env, value, "localeTag"), "localeTag");
    const std::string last_played =
        read_utf8_string(env, named_property(env, value, "lastPlayedId"), "lastPlayedId");
    locale_storage->assign(locale.begin(), locale.end());
    locale_storage->push_back('\0');
    last_played_storage->assign(last_played.begin(), last_played.end());
    last_played_storage->push_back('\0');

    fly_settings_snapshot snapshot{};
    snapshot.struct_size = FLY_SETTINGS_SNAPSHOT_V1_SIZE;
    snapshot.version = FLY_SETTINGS_SNAPSHOT_VERSION_1;
    snapshot.aspect_mode = static_cast<std::uint32_t>(
        read_int32(env, named_property(env, value, "aspectMode"), "aspectMode"));
    snapshot.video_quality_preset = static_cast<std::uint32_t>(read_int32(
        env, named_property(env, value, "videoQualityPreset"), "videoQualityPreset"));
    snapshot.custom_refresh_policy = static_cast<std::uint32_t>(read_int32(
        env, named_property(env, value, "customRefreshPolicy"), "customRefreshPolicy"));
    snapshot.custom_temporal_mode = static_cast<std::uint32_t>(read_int32(
        env, named_property(env, value, "customTemporalMode"), "customTemporalMode"));
    snapshot.custom_spatial_mode = static_cast<std::uint32_t>(read_int32(
        env, named_property(env, value, "customSpatialMode"), "customSpatialMode"));
    snapshot.custom_post_effect = static_cast<std::uint32_t>(read_int32(
        env, named_property(env, value, "customPostEffect"), "customPostEffect"));
    snapshot.adaptive_protection = static_cast<std::uint32_t>(read_int32(
        env, named_property(env, value, "adaptiveProtection"), "adaptiveProtection"));
    snapshot.layout_preset = static_cast<std::uint32_t>(
        read_int32(env, named_property(env, value, "layoutPreset"), "layoutPreset"));
    snapshot.direction_mode = static_cast<std::uint32_t>(
        read_int32(env, named_property(env, value, "directionMode"), "directionMode"));
    snapshot.button_scale =
        static_cast<float>(read_double(env, named_property(env, value, "buttonScale"), "buttonScale"));
    snapshot.vertical_offset = static_cast<float>(
        read_double(env, named_property(env, value, "verticalOffset"), "verticalOffset"));
    snapshot.control_opacity = static_cast<float>(
        read_double(env, named_property(env, value, "controlOpacity"), "controlOpacity"));
    snapshot.joystick_scale = static_cast<float>(
        read_double(env, named_property(env, value, "joystickScale"), "joystickScale"));
    snapshot.dead_zone =
        static_cast<float>(read_double(env, named_property(env, value, "deadZone"), "deadZone"));
    snapshot.haptic_level = static_cast<std::uint32_t>(
        read_int32(env, named_property(env, value, "hapticLevel"), "hapticLevel"));
    snapshot.distinct_ab_haptics = static_cast<std::uint32_t>(read_int32(
        env, named_property(env, value, "distinctAbHaptics"), "distinctAbHaptics"));
    snapshot.audio_enabled = static_cast<std::uint32_t>(
        read_int32(env, named_property(env, value, "audioEnabled"), "audioEnabled"));
    snapshot.audio_focus_policy = static_cast<std::uint32_t>(read_int32(
        env, named_property(env, value, "audioFocusPolicy"), "audioFocusPolicy"));
    snapshot.autosave_enabled = static_cast<std::uint32_t>(read_int32(
        env, named_property(env, value, "autosaveEnabled"), "autosaveEnabled"));
    snapshot.locale_tag_utf8 = locale_storage->data();
    snapshot.last_played_id_utf8 = last_played_storage->data();
    snapshot.locale_tag_capacity = static_cast<std::uint32_t>(locale_storage->size());
    snapshot.last_played_id_capacity = static_cast<std::uint32_t>(last_played_storage->size());
    snapshot.locale_tag_utf8_length = static_cast<std::uint32_t>(locale.size());
    snapshot.last_played_id_utf8_length = static_cast<std::uint32_t>(last_played.size());
    return snapshot;
}

napi_value make_settings_dto(napi_env env)
{
    fly_app_t& app = require_app();
    std::array<char, FLY_SETTINGS_LOCALE_MAX_UTF8_BYTES + 1u> locale{};
    std::array<char, FLY_CANONICAL_ID_MAX_UTF8_BYTES + 1u> last_played{};
    fly_settings_snapshot snapshot{};
    snapshot.struct_size = FLY_SETTINGS_SNAPSHOT_V1_SIZE;
    snapshot.version = FLY_SETTINGS_SNAPSHOT_VERSION_1;
    snapshot.locale_tag_utf8 = locale.data();
    snapshot.locale_tag_capacity = static_cast<std::uint32_t>(locale.size());
    snapshot.last_played_id_utf8 = last_played.data();
    snapshot.last_played_id_capacity = static_cast<std::uint32_t>(last_played.size());
    require_fly(fly_settings_get(&app, &snapshot), "fly_settings_get");

    napi_value result = nullptr;
    require_napi(napi_create_object(env, &result), "create SettingsDto");
    require_napi(napi_set_named_property(
                     env, result, "aspectMode",
                     create_uint32(env, snapshot.aspect_mode, "create aspectMode")),
                 "set aspectMode");
    require_napi(napi_set_named_property(
                     env, result, "videoQualityPreset",
                     create_uint32(env, snapshot.video_quality_preset, "create videoQualityPreset")),
                 "set videoQualityPreset");
    require_napi(napi_set_named_property(
                     env, result, "customRefreshPolicy",
                     create_uint32(env, snapshot.custom_refresh_policy, "create customRefreshPolicy")),
                 "set customRefreshPolicy");
    require_napi(napi_set_named_property(
                     env, result, "customTemporalMode",
                     create_uint32(env, snapshot.custom_temporal_mode, "create customTemporalMode")),
                 "set customTemporalMode");
    require_napi(napi_set_named_property(
                     env, result, "customSpatialMode",
                     create_uint32(env, snapshot.custom_spatial_mode, "create customSpatialMode")),
                 "set customSpatialMode");
    require_napi(napi_set_named_property(
                     env, result, "customPostEffect",
                     create_uint32(env, snapshot.custom_post_effect, "create customPostEffect")),
                 "set customPostEffect");
    require_napi(napi_set_named_property(
                     env, result, "adaptiveProtection",
                     create_uint32(env, snapshot.adaptive_protection, "create adaptiveProtection")),
                 "set adaptiveProtection");
    require_napi(napi_set_named_property(
                     env, result, "layoutPreset",
                     create_uint32(env, snapshot.layout_preset, "create layoutPreset")),
                 "set layoutPreset");
    require_napi(napi_set_named_property(
                     env, result, "directionMode",
                     create_uint32(env, snapshot.direction_mode, "create directionMode")),
                 "set directionMode");
    require_napi(napi_set_named_property(
                     env, result, "buttonScale",
                     create_double(env, snapshot.button_scale, "create buttonScale")),
                 "set buttonScale");
    require_napi(napi_set_named_property(
                     env, result, "verticalOffset",
                     create_double(env, snapshot.vertical_offset, "create verticalOffset")),
                 "set verticalOffset");
    require_napi(napi_set_named_property(
                     env, result, "controlOpacity",
                     create_double(env, snapshot.control_opacity, "create controlOpacity")),
                 "set controlOpacity");
    require_napi(napi_set_named_property(
                     env, result, "joystickScale",
                     create_double(env, snapshot.joystick_scale, "create joystickScale")),
                 "set joystickScale");
    require_napi(napi_set_named_property(
                     env, result, "deadZone",
                     create_double(env, snapshot.dead_zone, "create deadZone")),
                 "set deadZone");
    require_napi(napi_set_named_property(
                     env, result, "hapticLevel",
                     create_uint32(env, snapshot.haptic_level, "create hapticLevel")),
                 "set hapticLevel");
    require_napi(napi_set_named_property(
                     env, result, "distinctAbHaptics",
                     create_uint32(env, snapshot.distinct_ab_haptics, "create distinctAbHaptics")),
                 "set distinctAbHaptics");
    require_napi(napi_set_named_property(
                     env, result, "audioEnabled",
                     create_uint32(env, snapshot.audio_enabled, "create audioEnabled")),
                 "set audioEnabled");
    require_napi(napi_set_named_property(
                     env, result, "audioFocusPolicy",
                     create_uint32(env, snapshot.audio_focus_policy, "create audioFocusPolicy")),
                 "set audioFocusPolicy");
    require_napi(napi_set_named_property(
                     env, result, "autosaveEnabled",
                     create_uint32(env, snapshot.autosave_enabled, "create autosaveEnabled")),
                 "set autosaveEnabled");
    require_napi(napi_set_named_property(
                     env, result, "localeTag",
                     create_string(env, std::string(locale.data()), "create localeTag")),
                 "set localeTag");
    require_napi(napi_set_named_property(
                     env, result, "lastPlayedId",
                     create_string(env, std::string(last_played.data()), "create lastPlayedId")),
                 "set lastPlayedId");
    return result;
}

napi_value AppOpen(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 2;
        napi_value arguments[2] = {nullptr, nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read appOpen arguments");
        if (argument_count < 2)
        {
            throw NapiTypeError("appOpen requires dataRoot and cacheRoot strings");
        }
        const std::string data_root = read_root(env, arguments[0], "dataRoot");
        const std::string cache_root = read_root(env, arguments[1], "cacheRoot");
        open_app(data_root, cache_root);
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "appOpen undefined");
        return undefined;
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "appOpen failed: unknown native error", false);
    }
}

napi_value AppClose(napi_env env, napi_callback_info info)
{
    try
    {
        (void)info;
        g_scan.reset();
        g_app.reset();
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "appClose undefined");
        return undefined;
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "appClose failed: unknown native error", false);
    }
}

napi_value ControlLayoutGet(napi_env env, napi_callback_info info)
{
    try
    {
        (void)info;
        return create_string(env, control_layout_get_utf8(), "create controlLayoutGet");
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "controlLayoutGet failed: unknown native error", false);
    }
}

napi_value ControlLayoutApply(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read controlLayoutApply arguments");
        if (argument_count < 1)
        {
            throw NapiTypeError("controlLayoutApply requires a layout string");
        }
        const std::string utf8 = read_utf8_string(env, arguments[0], "utf8");
        require_fly(fly_control_layout_apply(&require_app(), utf8.data(),
                                             static_cast<std::uint32_t>(utf8.size())),
                    "fly_control_layout_apply");
        return create_string(env, control_layout_get_utf8(), "create applied layout");
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "controlLayoutApply failed: unknown native error", false);
    }
}

napi_value SettingsGet(napi_env env, napi_callback_info info)
{
    try
    {
        (void)info;
        return make_settings_dto(env);
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "settingsGet failed: unknown native error", false);
    }
}

napi_value SettingsApply(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read settingsApply arguments");
        if (argument_count < 1)
        {
            throw NapiTypeError("settingsApply requires a settings snapshot");
        }
        std::vector<char> locale;
        std::vector<char> last_played;
        const fly_settings_snapshot snapshot =
            read_settings_dto(env, arguments[0], &locale, &last_played);
        require_fly(fly_settings_apply(&require_app(), &snapshot), "fly_settings_apply");
        return make_settings_dto(env);
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "settingsApply failed: unknown native error", false);
    }
}

napi_value SourceStatusList(napi_env env, napi_callback_info info)
{
    try
    {
        (void)info;
        fly_app_t& app = require_app();
        std::uint64_t count = 0;
        require_fly(fly_source_status_count(&app, &count), "fly_source_status_count");
        napi_value result = nullptr;
        require_napi(napi_create_array_with_length(env, static_cast<std::size_t>(count), &result),
                     "create sourceStatusList");
        for (std::uint64_t index = 0; index < count; ++index)
        {
            fly_source_status status{};
            status.struct_size = FLY_SOURCE_STATUS_V1_SIZE;
            status.version = FLY_SOURCE_STATUS_VERSION_1;
            require_fly(fly_source_status_get(&app, index, &status), "fly_source_status_get");
            napi_value item = nullptr;
            require_napi(napi_create_object(env, &item), "create SourceStatusDto");
            require_napi(napi_set_named_property(
                             env, item, "uuidHex",
                             create_string(env, uuid_to_hex(status.source_uuid), "create uuidHex")),
                         "set uuidHex");
            require_napi(napi_set_named_property(
                             env, item, "sourceScope",
                             create_uint32(env, status.source_scope, "create sourceScope")),
                         "set sourceScope");
            require_napi(napi_set_named_property(
                             env, item, "lastCompleteness",
                             create_uint32(env, status.last_completeness, "create lastCompleteness")),
                         "set lastCompleteness");
            require_napi(napi_set_named_property(
                             env, item, "freshness",
                             create_uint32(env, status.freshness, "create freshness")),
                         "set freshness");
            require_napi(napi_set_element(env, result, static_cast<std::uint32_t>(index), item),
                         "set sourceStatusList element");
        }
        return result;
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "sourceStatusList failed: unknown native error", false);
    }
}

napi_value ScanBegin(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 2;
        napi_value arguments[2] = {nullptr, nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read scanBegin arguments");
        if (argument_count < 2)
        {
            throw NapiTypeError("scanBegin requires sourceUuidHex and sourceScope");
        }
        const std::string hex = read_utf8_string(env, arguments[0], "sourceUuidHex");
        const std::int32_t scope = read_int32(env, arguments[1], "sourceScope");
        fly_scan_config config{};
        config.struct_size = FLY_SCAN_CONFIG_V1_SIZE;
        config.version = FLY_SCAN_CONFIG_VERSION_1;
        parse_uuid_hex(hex, config.source_uuid);
        config.source_scope = static_cast<std::uint32_t>(scope);
        g_scan.reset();
        fly_scan_t* raw_scan = nullptr;
        require_fly(fly_scan_begin(&require_app(), &config, &raw_scan), "fly_scan_begin");
        if (raw_scan == nullptr)
        {
            throw FlyCallError("fly_scan_begin invariant", FLY_RESULT_INTERNAL_ERROR);
        }
        g_scan.reset(raw_scan);
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "scanBegin undefined");
        return undefined;
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "scanBegin failed: unknown native error", false);
    }
}

napi_value ScanAddFile(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 4;
        napi_value arguments[4] = {nullptr, nullptr, nullptr, nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read scanAddFile arguments");
        if (argument_count < 4)
        {
            throw NapiTypeError(
                "scanAddFile requires relativePath, displayName, borrowedFd, and declaredSize");
        }
        if (g_scan == nullptr)
        {
            throw NapiTypeError("scan is not open");
        }
        const std::string relative = read_utf8_string(env, arguments[0], "relativePath");
        const std::string display = read_utf8_string(env, arguments[1], "displayName");
        const std::int32_t borrowed_fd = read_int32(env, arguments[2], "borrowedFd");
        const std::int64_t declared_size = read_int64(env, arguments[3], "declaredSize");
        fly_scan_file file{};
        file.struct_size = FLY_SCAN_FILE_V1_SIZE;
        file.version = FLY_SCAN_FILE_VERSION_1;
        file.source_relative_path_utf8 = relative.data();
        file.display_name_utf8 = display.data();
        file.source_relative_path_utf8_length = static_cast<std::uint32_t>(relative.size());
        file.display_name_utf8_length = static_cast<std::uint32_t>(display.size());
        file.borrowed_fd = borrowed_fd;
        file.declared_size = declared_size < 0 ? 0 : static_cast<std::uint64_t>(declared_size);
        fly_scan_file_result scan_result{};
        scan_result.struct_size = FLY_SCAN_FILE_RESULT_V1_SIZE;
        scan_result.version = FLY_SCAN_FILE_RESULT_VERSION_1;
        require_fly(fly_scan_add_file(g_scan.get(), &file, &scan_result), "fly_scan_add_file");
        napi_value result = nullptr;
        require_napi(napi_create_object(env, &result), "create ScanFileResultDto");
        require_napi(napi_set_named_property(
                         env, result, "outcome",
                         create_uint32(env, scan_result.outcome, "create outcome")),
                     "set outcome");
        require_napi(napi_set_named_property(
                         env, result, "reason",
                         create_uint32(env, scan_result.reason, "create reason")),
                     "set reason");
        require_napi(napi_set_named_property(
                         env, result, "variantCount",
                         create_uint32(env, scan_result.variant_count, "create variantCount")),
                     "set variantCount");
        return result;
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "scanAddFile failed: unknown native error", false);
    }
}

napi_value ScanCommit(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read scanCommit arguments");
        if (argument_count < 1)
        {
            throw NapiTypeError("scanCommit requires completeness");
        }
        if (g_scan == nullptr)
        {
            throw NapiTypeError("scan is not open");
        }
        const std::int32_t completeness = read_int32(env, arguments[0], "completeness");
        fly_scan_t* scan = g_scan.release();
        require_fly(fly_scan_commit(scan, static_cast<std::uint32_t>(completeness)),
                    "fly_scan_commit");
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "scanCommit undefined");
        return undefined;
    }
    catch (const NapiTypeError& error)
    {
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        g_scan.reset();
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        g_scan.reset();
        return report_error(env, "scanCommit failed: unknown native error", false);
    }
}

napi_value ScanAbort(napi_env env, napi_callback_info info)
{
    try
    {
        (void)info;
        g_scan.reset();
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "scanAbort undefined");
        return undefined;
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "scanAbort failed: unknown native error", false);
    }
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
            {"gameCenterFilter", nullptr, GameCenterFilter, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"controlLayoutRecommended", nullptr, ControlLayoutRecommended, nullptr, nullptr, nullptr,
             napi_default, nullptr},
            {"controlLayoutDecodeOrRecommended", nullptr, ControlLayoutDecodeOrRecommended, nullptr,
             nullptr, nullptr, napi_default, nullptr},
            {"controlLayoutGet", nullptr, ControlLayoutGet, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"controlLayoutApply", nullptr, ControlLayoutApply, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"hitMapFromLayout", nullptr, HitMapFromLayout, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"pauseCommands", nullptr, PauseCommands, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"appOpen", nullptr, AppOpen, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"appClose", nullptr, AppClose, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"settingsGet", nullptr, SettingsGet, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"settingsApply", nullptr, SettingsApply, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"sourceStatusList", nullptr, SourceStatusList, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"scanBegin", nullptr, ScanBegin, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"scanAddFile", nullptr, ScanAddFile, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"scanCommit", nullptr, ScanCommit, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"scanAbort", nullptr, ScanAbort, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playOpen", nullptr, PlayOpen, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playSetButtons", nullptr, PlaySetButtons, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playStep", nullptr, PlayStep, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playSaveCheckpoint", nullptr, PlaySaveCheckpoint, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playClose", nullptr, PlayClose, nullptr, nullptr, nullptr, napi_default, nullptr},
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
