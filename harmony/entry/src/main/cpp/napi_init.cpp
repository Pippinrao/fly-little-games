#include "catalog_smoke.hpp"
#include "play_session.hpp"
#include "product_bridge.hpp"

#include <flynes/flynes_app.h>

#include "napi/native_api.h"

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

napi_value PlayLoadCheckpoint(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read playLoadCheckpoint arguments");
        if (argument_count < 1)
        {
            throw NapiTypeError("playLoadCheckpoint requires checkpoint bytes");
        }
        const std::vector<std::uint8_t> bytes = read_buffer(env, arguments[0], "checkpoint");
        require_play().load_checkpoint(bytes.data(), bytes.size());
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "playLoadCheckpoint undefined");
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
        return report_error(env, "playLoadCheckpoint failed: unknown native error", false);
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
            {"hitMapFromLayout", nullptr, HitMapFromLayout, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"pauseCommands", nullptr, PauseCommands, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"playOpen", nullptr, PlayOpen, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playSetButtons", nullptr, PlaySetButtons, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playStep", nullptr, PlayStep, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playSaveCheckpoint", nullptr, PlaySaveCheckpoint, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playLoadCheckpoint", nullptr, PlayLoadCheckpoint, nullptr, nullptr, nullptr, napi_default, nullptr},
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
