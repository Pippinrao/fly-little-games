#include "catalog_smoke.hpp"
#include "harmony_renderer.hpp"
#include "play_package.hpp"
#include "play_session.hpp"
#include "native_play_runtime.hpp"
#include "product_bridge.hpp"
#include "scan_job_executor.hpp"
#include "scan_job_queue.hpp"

#include <flynes/flynes_app.h>
#include <flynes/flynes_session.h>
#include "flynes/product/game_center_state.hpp"

#include "napi/native_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

[[maybe_unused]] void verify_nearby_v2_composition_contract()
{
    fly_session_clock_port_v2 clock{};
    clock.struct_size = FLY_SESSION_CLOCK_PORT_V2_SIZE;
    clock.abi_version = FLY_SESSION_ABI_VERSION_2;
    fly_session_executor_port_v2 executor{};
    executor.struct_size = FLY_SESSION_EXECUTOR_PORT_V2_SIZE;
    executor.abi_version = FLY_SESSION_ABI_VERSION_2;
    fly_session_platform_state_port_v2 platform_state{};
    platform_state.struct_size = FLY_SESSION_PLATFORM_STATE_PORT_V2_SIZE;
    platform_state.abi_version = FLY_SESSION_ABI_VERSION_2;
    fly_session_ports_v2 ports{};
    ports.struct_size = FLY_SESSION_PORTS_V2_SIZE;
    ports.abi_version = FLY_SESSION_ABI_VERSION_2;
    ports.clock = &clock;
    ports.executor = &executor;
    ports.platform_state = &platform_state;
    (void)ports;
}

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

std::unique_ptr<flynes::harmony::NativePlayRuntime> g_play;

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

struct SessionDeleter final
{
    void operator()(fly_session_t* session) const noexcept
    {
        fly_session_destroy(session);
    }
};

struct SnapshotDeleter final
{
    void operator()(fly_catalog_snapshot_t* snapshot) const noexcept
    {
        fly_catalog_snapshot_release(snapshot);
    }
};

std::unique_ptr<fly_app_t, AppDeleter> g_app;
std::unique_ptr<fly_scan_t, ScanDeleter> g_scan;
std::unique_ptr<flynes::harmony::ScanJobQueue> g_scan_jobs;
std::unique_ptr<fly_session_t, SessionDeleter> g_nearby_session;
std::uint64_t g_nearby_next_host_generation = 1u;
std::uint64_t g_nearby_next_join_attempt_id = 1u;

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

std::int64_t read_int64(napi_env env, napi_value value, const char* argument_name);

fly_session_t& require_nearby_session()
{
    if (g_nearby_session == nullptr)
    {
        const fly_session_config config{
            FLY_SESSION_CONFIG_V1_SIZE,
            FLY_SESSION_CONFIG_VERSION_1,
            0u,
            0u,
        };
        fly_session_t* raw_session = nullptr;
        require_fly(fly_session_create(&config, &raw_session), "fly_session_create");
        if (raw_session == nullptr)
        {
            throw FlyCallError("fly_session_create invariant", FLY_RESULT_INTERNAL_ERROR);
        }
        g_nearby_session.reset(raw_session);
    }
    return *g_nearby_session;
}

std::uint64_t invite_id(napi_env env, napi_value value, const char* argument_name)
{
    const std::int64_t parsed = read_int64(env, value, argument_name);
    if (parsed <= 0)
    {
        throw NapiTypeError(std::string(argument_name) + " must be positive");
    }
    return static_cast<std::uint64_t>(parsed);
}

std::uint64_t uptime_ns(napi_env env, napi_value value)
{
    const std::int64_t milliseconds = read_int64(env, value, "nowMs");
    constexpr std::uint64_t kNanosecondsPerMillisecond = UINT64_C(1000000);
    if (milliseconds < 0 || static_cast<std::uint64_t>(milliseconds) >
            std::numeric_limits<std::uint64_t>::max() / kNanosecondsPerMillisecond)
    {
        throw NapiTypeError("nowMs is outside the supported monotonic range");
    }
    return static_cast<std::uint64_t>(milliseconds) * kNanosecondsPerMillisecond;
}

void resolve_pending_invite_command(fly_session_t& session, bool success)
{
    fly_session_command command{};
    command.struct_size = FLY_SESSION_COMMAND_V1_SIZE;
    command.version = FLY_SESSION_COMMAND_VERSION_1;
    require_fly(fly_session_poll_command(&session, &command), "fly_session_poll_command");
    if (command.command_id == 0u)
    {
        throw NapiTypeError("shared invite action did not issue a command");
    }
    const fly_session_command_result result{
        FLY_SESSION_COMMAND_RESULT_V1_SIZE,
        FLY_SESSION_COMMAND_RESULT_VERSION_1,
        command.command_id,
        0u,
        success ? FLY_RESULT_OK : FLY_RESULT_INVALID_STATE,
        0u,
    };
    require_fly(fly_session_complete_command(&session, &result),
                "fly_session_complete_command");
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
    g_scan_jobs = std::make_unique<flynes::harmony::ScanJobQueue>(
        [raw_app](const flynes::harmony::ScanJobRequest& request,
                  const flynes::harmony::ScanJobQueue::CancelCheck& cancelled,
                  const flynes::harmony::ScanJobQueue::ProgressSink& progress) {
            return flynes::harmony::execute_scan_job(*raw_app, request, cancelled, progress);
        },
        [](int fd) { if (fd >= 0) close(fd); });
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
    bool has_aliases = false;
    require_napi(napi_has_named_property(env, value, "searchAliases", &has_aliases), "inspect searchAliases");
    if (has_aliases)
        row.search_aliases = read_utf8_string(env, named_property(env, value, "searchAliases"), "searchAliases");
    row.source_uuid_hex = read_utf8_string(
        env, named_property(env, value, "sourceUuidHex"), "sourceUuidHex");
    row.source_relative_path = read_utf8_string(
        env, named_property(env, value, "sourceRelativePath"), "sourceRelativePath");
    row.package_format = read_int32(
        env, named_property(env, value, "packageFormat"), "packageFormat");
    bool has_popularity = false;
    require_napi(napi_has_named_property(env, value, "popularityScore", &has_popularity),
                 "inspect popularityScore");
    if (has_popularity)
        row.popularity_score = read_int32(
            env, named_property(env, value, "popularityScore"), "popularityScore");
    return row;
}

napi_value make_game_center_row(napi_env env, const flynes::harmony::GameCenterRow& row)
{
    napi_value result = nullptr;
    require_napi(napi_create_object(env, &result), "create GameCenterRow");
    require_napi(napi_set_named_property(env, result, "searchAliases",
        create_string(env, row.search_aliases, "create searchAliases")), "set searchAliases");
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
    require_napi(napi_set_named_property(
                     env, result, "sourceUuidHex",
                     create_string(env, row.source_uuid_hex, "create sourceUuidHex")),
                 "set sourceUuidHex");
    require_napi(napi_set_named_property(
                     env, result, "sourceRelativePath",
                     create_string(env, row.source_relative_path, "create sourceRelativePath")),
                 "set sourceRelativePath");
    require_napi(napi_set_named_property(
                     env, result, "packageFormat",
                     create_int64(env, row.package_format, "create packageFormat")),
                 "set packageFormat");
    require_napi(napi_set_named_property(
                     env, result, "popularityScore",
                     create_int64(env, row.popularity_score, "create popularityScore")),
                 "set popularityScore");
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

flynes::harmony::NativePlayRuntime& require_play()
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
        g_play = flynes::harmony::NativePlayRuntime::open(rom.data(), rom.size());
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

napi_value PlayDecodePackage(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 3;
        napi_value arguments[3] = {nullptr, nullptr, nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read playDecodePackage arguments");
        if (argument_count < 3)
        {
            throw NapiTypeError("playDecodePackage requires bytes, packageFormat, and zipEntryName");
        }
        const std::vector<std::uint8_t> physical = read_buffer(env, arguments[0], "rom");
        const std::int32_t package_format = read_int32(env, arguments[1], "packageFormat");
        const std::string zip_entry = read_utf8_string(env, arguments[2], "zipEntryName");
        const std::vector<std::uint8_t> payload = flynes::harmony::decode_rom_package(
            physical.data(), physical.size(), static_cast<std::uint32_t>(package_format), zip_entry);
        return create_arraybuffer(env, payload.data(), payload.size(), "playDecodePackage payload");
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
        return report_error(env, "playDecodePackage failed: unknown native error", false);
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
        require_play().set_buttons(buttons);
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
        const flynes::harmony::PlayStepResult step = require_play().copy_latest_frame();
        return make_play_result(env, step);
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

napi_value PlaySetPaused(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read playSetPaused arguments");
        if (argument_count < 1) throw NapiTypeError("playSetPaused requires paused");
        bool paused = false;
        require_napi(napi_get_value_bool(env, arguments[0], &paused), "read play paused");
        require_play().set_paused(paused);
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "playSetPaused undefined");
        return undefined;
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
}

napi_value PlaySetAudioMuted(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read playSetAudioMuted arguments");
        if (argument_count < 1) throw NapiTypeError("playSetAudioMuted requires muted");
        bool muted = false;
        require_napi(napi_get_value_bool(env, arguments[0], &muted), "read audio muted");
        require_play().set_muted(muted);
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "playSetAudioMuted undefined");
        return undefined;
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
}

napi_value PlayRuntimeStatus(napi_env env, napi_callback_info info)
{
    try
    {
        (void)info;
        const auto status = require_play().status();
        napi_value result = nullptr;
        require_napi(napi_create_object(env, &result), "create playRuntimeStatus");
        auto set_bool = [&](const char* name, bool value) {
            require_napi(napi_set_named_property(env, result, name,
                                                 create_bool(env, value, name)), name);
        };
        auto set_i64 = [&](const char* name, std::int64_t value) {
            require_napi(napi_set_named_property(env, result, name,
                                                 create_int64(env, value, name)), name);
        };
        set_bool("running", status.running);
        set_bool("paused", status.paused);
        set_bool("audioReady", status.audio_ready);
        set_bool("audioStarted", status.audio_started);
        set_bool("audioTimestampValid", status.audio_timestamp_valid);
        set_i64("sourceFrames", static_cast<std::int64_t>(status.source_frames));
        set_i64("audioUnderflows", static_cast<std::int64_t>(status.audio_underflows));
        set_i64("audioPostFallbackUnderflows",
                static_cast<std::int64_t>(status.audio_post_fallback_underflows));
        set_i64("audioLockMisses", static_cast<std::int64_t>(status.audio_lock_misses));
        set_i64("audioShortReads", static_cast<std::int64_t>(status.audio_short_reads));
        set_i64("audioPrimingCallbacks",
                static_cast<std::int64_t>(status.audio_priming_callbacks));
        set_i64("audioDroppedSamples", static_cast<std::int64_t>(status.audio_dropped_samples));
        set_i64("audioProducedSamples", static_cast<std::int64_t>(status.audio_produced_samples));
        set_i64("audioConsumedSamples", static_cast<std::int64_t>(status.audio_consumed_samples));
        set_i64("audioQueuedSamples", static_cast<std::int64_t>(status.audio_queued_samples));
        set_i64("audioHighWaterSamples", static_cast<std::int64_t>(status.audio_high_water_samples));
        set_i64("audioCallbackFrames", status.audio_callback_frames);
        set_i64("audioSampleRate", status.audio_sample_rate);
        set_i64("audioChannelCount", status.audio_channel_count);
        set_i64("audioLastCallbackBytes", status.audio_last_callback_bytes);
        set_i64("audioCallbackCount", static_cast<std::int64_t>(status.audio_callback_count));
        set_bool("audioFastPath", status.audio_fast_path);
        set_i64("audioDelaySamples", static_cast<std::int64_t>(status.audio_delay_samples));
        set_i64("audioFramePosition", status.audio_frame_position);
        set_i64("audioTimestampNanos", status.audio_timestamp_ns);
        require_napi(napi_set_named_property(env, result, "sourceFps",
                                             create_double(env, status.source_fps, "sourceFps")),
                     "sourceFps");
        require_napi(napi_set_named_property(env, result, "sourceStandard",
                                             create_string(env, status.source_standard,
                                                           "sourceStandard")),
                     "sourceStandard");
        require_napi(napi_set_named_property(
                         env, result, "audioTemporalState",
                         create_string(env, status.audio_temporal_state,
                                       "audioTemporalState")),
                     "audioTemporalState");
        require_napi(napi_set_named_property(
                         env, result, "audioFallbackReason",
                         create_string(env, status.audio_fallback_reason,
                                       "audioFallbackReason")),
                     "audioFallbackReason");
        require_napi(napi_set_named_property(env, result, "error",
                                             create_string(env, status.error, "play error")),
                     "play error");
        return result;
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
}

napi_value RenderConfigure(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 5;
        napi_value arguments[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read renderConfigure arguments");
        if (argument_count < 5)
        {
            throw NapiTypeError("renderConfigure requires refresh, temporal, spatial, post, and protection");
        }
        const std::int32_t refresh = read_int32(env, arguments[0], "refresh");
        const std::int32_t temporal = read_int32(env, arguments[1], "temporal");
        const std::int32_t spatial = read_int32(env, arguments[2], "spatial");
        const std::int32_t post = read_int32(env, arguments[3], "post");
        bool protection = false;
        require_napi(napi_get_value_bool(env, arguments[4], &protection), "read protection");
        flynes::harmony::harmony_renderer().configure(
            refresh, temporal, spatial, post, protection);
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "renderConfigure undefined");
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
}

napi_value RenderSetPaused(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read renderSetPaused arguments");
        if (argument_count < 1)
        {
            throw NapiTypeError("renderSetPaused requires paused");
        }
        bool paused = false;
        require_napi(napi_get_value_bool(env, arguments[0], &paused), "read paused");
        flynes::harmony::harmony_renderer().set_paused(paused);
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "renderSetPaused undefined");
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
}

napi_value RenderSetProtection(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 3;
        napi_value arguments[3] = {nullptr, nullptr, nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read renderSetProtection arguments");
        if (argument_count < 3)
        {
            throw NapiTypeError("renderSetProtection requires thermal, battery, and validity flags");
        }
        bool thermal_limited = false;
        bool low_battery = false;
        bool observation_valid = false;
        require_napi(napi_get_value_bool(env, arguments[0], &thermal_limited),
                     "read thermal protection");
        require_napi(napi_get_value_bool(env, arguments[1], &low_battery),
                     "read battery protection");
        require_napi(napi_get_value_bool(env, arguments[2], &observation_valid),
                     "read protection observation validity");
        flynes::harmony::harmony_renderer().set_protection(
            thermal_limited, low_battery, observation_valid);
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "renderSetProtection undefined");
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
}

napi_value RenderStatus(napi_env env, napi_callback_info info)
{
    try
    {
        (void)info;
        const flynes::harmony::HarmonyRenderStatus status =
            flynes::harmony::harmony_renderer().status();
        napi_value result = nullptr;
        require_napi(napi_create_object(env, &result), "create renderStatus result");
        auto set_bool = [&](const char* name, bool value) {
            require_napi(napi_set_named_property(
                             env, result, name, create_bool(env, value, name)),
                         "set renderStatus boolean");
        };
        auto set_i64 = [&](const char* name, std::int64_t value) {
            require_napi(napi_set_named_property(
                             env, result, name, create_int64(env, value, name)),
                         "set renderStatus integer");
        };
        set_bool("componentBound", status.component_bound);
        set_bool("surfaceReady", status.mailbox.surface_ready);
        set_bool("nativeReady", status.native_ready);
        set_bool("fallbackActive", status.fallback_active);
        set_bool("timingValid", status.timing_valid);
        set_bool("gpuTimingValid", status.gpu_timing_valid);
        set_bool("displayRequestAccepted", status.display.request_accepted);
        set_bool("displayObservationValid", status.display.observation_valid);
        set_bool("motionQualified", status.display.motion_qualified);
        set_bool("protectionObservationValid",
                 status.display.protection_observation_valid);
        set_bool("thermalLimited", status.display.thermal_limited);
        set_bool("lowBattery", status.display.low_battery);
        set_i64("surfaceGeneration", static_cast<std::int64_t>(status.mailbox.surface_generation));
        set_i64("sourceFrames", static_cast<std::int64_t>(status.mailbox.source_frames));
        set_i64("uploadedFrames", static_cast<std::int64_t>(status.mailbox.uploaded_frames));
        set_i64("presentedFrames", static_cast<std::int64_t>(status.mailbox.presented_frames));
        set_i64("presentFailures", static_cast<std::int64_t>(status.mailbox.present_failures));
        set_i64("requestedSpatial", status.requested_spatial);
        set_i64("effectiveSpatial", status.effective_spatial);
        set_i64("requestedPost", status.requested_post);
        set_i64("vsyncPeriodNanos", status.vsync_period_ns);
        set_i64("gpuTimeNanos", status.gpu_time_ns);
        set_i64("gpuTimeMaxNanos", status.gpu_time_max_ns);
        set_i64("gpuTimingSamples", static_cast<std::int64_t>(status.gpu_timing_samples));
        set_i64("requestedRefreshHz", status.display.requested_hz);
        set_i64("actualRefreshMilliHz", status.display.actual_millihz);
        set_i64("effectiveRefreshHz", status.display.effective_hz);
        set_i64("displayRequestGeneration",
                static_cast<std::int64_t>(status.display.request_generation));
        set_i64("motionSourceSlots", static_cast<std::int64_t>(status.motion.source_slots));
        set_i64("motionSynthesizedSlots",
                static_cast<std::int64_t>(status.motion.synthesized_slots));
        set_i64("motionHoldSlots", static_cast<std::int64_t>(status.motion.hold_slots));
        set_i64("motionAdjacentPairs", static_cast<std::int64_t>(status.motion.adjacent_pairs));
        require_napi(napi_set_named_property(
                         env, result, "fallbackReason",
                         create_string(env, status.fallback_reason, "fallbackReason")),
                     "set fallbackReason");
        require_napi(napi_set_named_property(
                         env, result, "displayFallbackReason",
                         create_string(env, status.display.fallback_reason,
                                       "displayFallbackReason")),
                     "set displayFallbackReason");
        require_napi(napi_set_named_property(
                         env, result, "temporalState",
                         create_string(env, status.temporal_state, "temporalState")),
                     "set temporalState");
        require_napi(napi_set_named_property(
                         env, result, "temporalFallbackReason",
                         create_string(env, status.temporal_fallback_reason,
                                       "temporalFallbackReason")),
                     "set temporalFallbackReason");
        return result;
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
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
        const std::vector<std::uint8_t> bytes = read_buffer(
            env, arguments[0], "checkpoint");
        require_play().load_checkpoint(bytes.data(), bytes.size());
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined),
                     "playLoadCheckpoint undefined");
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
        g_scan_jobs.reset();
        g_play.reset();
        g_scan.reset();
        g_app.reset();
        g_nearby_session.reset();
        g_nearby_next_host_generation = 1u;
        g_nearby_next_join_attempt_id = 1u;
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

napi_value SourceRemove(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read sourceRemove arguments");
        if (argument_count < 1)
        {
            throw NapiTypeError("sourceRemove requires sourceUuidHex");
        }
        const std::string hex = read_utf8_string(env, arguments[0], "sourceUuidHex");
        std::uint8_t uuid[16] = {};
        parse_uuid_hex(hex, uuid);
        fly_app_t& app = require_app();
        std::uint64_t count = 0;
        require_fly(fly_source_status_count(&app, &count), "fly_source_status_count");
        bool found = false;
        std::uint32_t scope = 0;
        for (std::uint64_t index = 0; index < count; ++index)
        {
            fly_source_status status{};
            status.struct_size = FLY_SOURCE_STATUS_V1_SIZE;
            status.version = FLY_SOURCE_STATUS_VERSION_1;
            require_fly(fly_source_status_get(&app, index, &status), "fly_source_status_get");
            bool match = true;
            for (std::size_t byte = 0; byte < 16; ++byte)
            {
                if (status.source_uuid[byte] != uuid[byte])
                {
                    match = false;
                    break;
                }
            }
            if (match)
            {
                found = true;
                scope = status.source_scope;
                break;
            }
        }
        if (!found)
        {
            throw FlyCallError("fly_source_remove", FLY_RESULT_NOT_FOUND);
        }
        if (g_scan_jobs != nullptr)
        {
            g_scan_jobs->cancel_source(hex, scope);
        }
        require_fly(fly_source_remove(&app, uuid, scope), "fly_source_remove");
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "sourceRemove undefined");
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
        return report_error(env, "sourceRemove failed: unknown native error", false);
    }
}

flynes::harmony::ScanJobQueue& require_scan_jobs()
{
    if (g_scan_jobs == nullptr)
    {
        throw NapiTypeError("app is not open");
    }
    return *g_scan_jobs;
}

void close_scan_job_files(flynes::harmony::ScanJobRequest& request) noexcept
{
    for (flynes::harmony::ScanJobFile& file : request.files)
    {
        if (file.owned_fd >= 0)
        {
            close(file.owned_fd);
            file.owned_fd = -1;
        }
    }
}

napi_value ScanJobStart(napi_env env, napi_callback_info info)
{
    flynes::harmony::ScanJobRequest request;
    try
    {
        std::size_t argument_count = 4;
        napi_value arguments[4] = {nullptr, nullptr, nullptr, nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read scanJobStart arguments");
        if (argument_count < 4)
        {
            throw NapiTypeError(
                "scanJobStart requires sourceUuidHex, sourceScope, files, and completeness");
        }
        request.source_uuid_hex = read_utf8_string(env, arguments[0], "sourceUuidHex");
        std::uint8_t parsed_uuid[16]{};
        parse_uuid_hex(request.source_uuid_hex, parsed_uuid);
        const std::int32_t scope = read_int32(env, arguments[1], "sourceScope");
        const std::int32_t completeness = read_int32(env, arguments[3], "completeness");
        if (scope < 0 || completeness < 0)
        {
            throw NapiTypeError("sourceScope and completeness must be non-negative");
        }
        request.source_scope = static_cast<std::uint32_t>(scope);
        request.final_completeness = static_cast<std::uint32_t>(completeness);

        bool is_array = false;
        require_napi(napi_is_array(env, arguments[2], &is_array), "inspect scan files array");
        if (!is_array) throw NapiTypeError("files must be an Array");
        std::uint32_t count = 0u;
        require_napi(napi_get_array_length(env, arguments[2], &count), "measure scan files array");
        request.files.reserve(count);
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            napi_value item = nullptr;
            require_napi(napi_get_element(env, arguments[2], index, &item), "read scan file item");
            flynes::harmony::ScanJobFile file;
            file.relative_path = read_utf8_string(
                env, named_property(env, item, "relativePath"), "relativePath");
            file.display_name = read_utf8_string(
                env, named_property(env, item, "displayName"), "displayName");
            const std::int32_t borrowed_fd = read_int32(
                env, named_property(env, item, "borrowedFd"), "borrowedFd");
            const std::int64_t declared_size = read_int64(
                env, named_property(env, item, "declaredSize"), "declaredSize");
            if (borrowed_fd < 0 || declared_size < 0)
            {
                throw NapiTypeError("borrowedFd and declaredSize must be non-negative");
            }
            file.owned_fd = dup(borrowed_fd);
            if (file.owned_fd < 0)
            {
                throw std::runtime_error("dup borrowedFd failed");
            }
            file.declared_size = static_cast<std::uint64_t>(declared_size);
            request.files.push_back(std::move(file));
        }
        const std::uint64_t id = require_scan_jobs().start(std::move(request));
        return create_int64(env, static_cast<std::int64_t>(id), "create scan job id");
    }
    catch (const NapiTypeError& error)
    {
        close_scan_job_files(request);
        return report_error(env, error.what(), true);
    }
    catch (const std::exception& error)
    {
        close_scan_job_files(request);
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        close_scan_job_files(request);
        return report_error(env, "scanJobStart failed: unknown native error", false);
    }
}

napi_value ScanJobStatus(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read scanJobStatus arguments");
        if (argument_count < 1) throw NapiTypeError("scanJobStatus requires jobId");
        const std::int64_t signed_id = read_int64(env, arguments[0], "jobId");
        if (signed_id <= 0) throw NapiTypeError("jobId must be positive");
        const auto status = require_scan_jobs().status(static_cast<std::uint64_t>(signed_id));
        if (status.phase == flynes::harmony::ScanJobPhase::UNKNOWN)
        {
            throw NapiTypeError("scan job was not found");
        }
        napi_value result = nullptr;
        require_napi(napi_create_object(env, &result), "create ScanJobStatusDto");
        require_napi(napi_set_named_property(
                         env, result, "id",
                         create_int64(env, static_cast<std::int64_t>(status.id), "create scan id")),
                     "set scan id");
        require_napi(napi_set_named_property(
                         env, result, "phase",
                         create_uint32(env, static_cast<std::uint32_t>(status.phase),
                                       "create scan phase")),
                     "set scan phase");
        require_napi(napi_set_named_property(
                         env, result, "processedFiles",
                         create_int64(env, static_cast<std::int64_t>(status.processed_files),
                                      "create processed files")),
                     "set processed files");
        require_napi(napi_set_named_property(
                         env, result, "resultCount",
                         create_int64(env, static_cast<std::int64_t>(status.result_count),
                                      "create result count")),
                     "set result count");
        require_napi(napi_set_named_property(
                         env, result, "cancelRequested",
                         create_bool(env, status.cancel_requested, "create cancel requested")),
                     "set cancel requested");
        require_napi(napi_set_named_property(
                         env, result, "error",
                         create_string(env, status.error, "create scan error")),
                     "set scan error");
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
        return report_error(env, "scanJobStatus failed: unknown native error", false);
    }
}

napi_value ScanJobCancel(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read scanJobCancel arguments");
        if (argument_count < 1) throw NapiTypeError("scanJobCancel requires jobId");
        const std::int64_t signed_id = read_int64(env, arguments[0], "jobId");
        if (signed_id <= 0) throw NapiTypeError("jobId must be positive");
        return create_bool(env,
                           require_scan_jobs().cancel(static_cast<std::uint64_t>(signed_id)),
                           "create scan cancellation result");
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
        return report_error(env, "scanJobCancel failed: unknown native error", false);
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
        const fly_result add_status =
            fly_scan_add_file(g_scan.get(), &file, &scan_result);
        if (add_status != FLY_RESULT_OK)
        {
            throw FlyCallError(
                (std::string("fly_scan_add_file path=") + relative + " display=" + display +
                 " fd=" + std::to_string(borrowed_fd) + " size=" +
                 std::to_string(declared_size) + " struct=" +
                 std::to_string(static_cast<unsigned>(FLY_SCAN_FILE_V1_SIZE)))
                    .c_str(),
                add_status);
        }
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

std::string basename_utf8(const std::string& path)
{
    const std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
    {
        return path;
    }
    return path.substr(slash + 1u);
}

/** Android RomPackageScanner.titleLanguage parity: any HAN code point → zh-Hans title. */
bool contains_han_script(const std::string& value)
{
    // Iterate UTF-8 code points; HAN ranges: 4E00–9FFF, 3400–4DBF, F900–FAFF,
    // 20000–2A6DF (supplementary, encoded as 4-byte sequences).
    std::size_t index = 0u;
    while (index < value.size())
    {
        const unsigned char byte0 = static_cast<unsigned char>(value[index]);
        std::uint32_t code_point = 0u;
        std::size_t advance = 1u;
        if (byte0 < 0x80u)
        {
            code_point = byte0;
        }
        else if ((byte0 & 0xE0u) == 0xC0u && index + 1u < value.size())
        {
            code_point = (static_cast<std::uint32_t>(byte0 & 0x1Fu) << 6u) |
                static_cast<std::uint32_t>(static_cast<unsigned char>(value[index + 1u]) & 0x3Fu);
            advance = 2u;
        }
        else if ((byte0 & 0xF0u) == 0xE0u && index + 2u < value.size())
        {
            code_point = (static_cast<std::uint32_t>(byte0 & 0x0Fu) << 12u) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(value[index + 1u]) & 0x3Fu) << 6u) |
                static_cast<std::uint32_t>(static_cast<unsigned char>(value[index + 2u]) & 0x3Fu);
            advance = 3u;
        }
        else if ((byte0 & 0xF8u) == 0xF0u && index + 3u < value.size())
        {
            code_point = (static_cast<std::uint32_t>(byte0 & 0x07u) << 18u) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(value[index + 1u]) & 0x3Fu) << 12u) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(value[index + 2u]) & 0x3Fu) << 6u) |
                static_cast<std::uint32_t>(static_cast<unsigned char>(value[index + 3u]) & 0x3Fu);
            advance = 4u;
        }
        if ((code_point >= 0x4E00u && code_point <= 0x9FFFu) ||
            (code_point >= 0x3400u && code_point <= 0x4DBFu) ||
            (code_point >= 0xF900u && code_point <= 0xFAFFu) ||
            (code_point >= 0x20000u && code_point <= 0x2A6DFu))
        {
            return true;
        }
        index += advance;
    }
    return false;
}

flynes::harmony::GameCenterRow row_from_catalog_entry(fly_app_t& app, const fly_catalog_entry& entry)
{
    flynes::harmony::GameCenterRow row;
    row.canonical_id = entry.canonical_id_utf8 == nullptr ? std::string{} : entry.canonical_id_utf8;
    const std::string display =
        entry.display_name_utf8 == nullptr ? std::string{} : entry.display_name_utf8;
    const std::string relative = entry.source_relative_path_utf8 == nullptr
                                     ? std::string{}
                                     : entry.source_relative_path_utf8;
    row.original_filename = !display.empty() ? display : basename_utf8(relative);
    row.popularity_score = flynes::product::popularity_for_package(relative, display);
    row.title_en = !display.empty() ? display : row.original_filename;
    // Android RomPackageScanner.titleLanguage parity: a title containing HAN
    // script characters is a zh-Hans title; Latin-only titles stay EN.
    row.title_zh_hans = contains_han_script(row.title_en) ? row.title_en : std::string{};
    row.builtin = entry.source_scope == FLY_SOURCE_SCOPE_BUILTIN;
    row.source_uuid_hex = uuid_to_hex(entry.source_uuid);
    row.source_relative_path = relative;
    row.package_format = static_cast<int>(entry.package_format);

    fly_catalog_user_state user{};
    user.struct_size = FLY_CATALOG_USER_STATE_V1_SIZE;
    user.version = FLY_CATALOG_USER_STATE_VERSION_1;
    require_fly(fly_catalog_user_state_get(&app, row.canonical_id.data(),
                                           static_cast<std::uint32_t>(row.canonical_id.size()),
                                           &user),
                "fly_catalog_user_state_get");
    row.favorite = user.favorite != 0;
    if (user.last_played_sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
        row.last_played_sequence = std::numeric_limits<std::int64_t>::max();
    }
    else
    {
        row.last_played_sequence = static_cast<std::int64_t>(user.last_played_sequence);
    }
    return row;
}

napi_value CatalogSnapshot(napi_env env, napi_callback_info info)
{
    try
    {
        (void)info;
        fly_app_t& app = require_app();
        fly_catalog_snapshot_t* raw_snapshot = nullptr;
        require_fly(fly_catalog_snapshot(&app, &raw_snapshot), "fly_catalog_snapshot");
        if (raw_snapshot == nullptr)
        {
            throw FlyCallError("fly_catalog_snapshot invariant", FLY_RESULT_INTERNAL_ERROR);
        }
        std::unique_ptr<fly_catalog_snapshot_t, SnapshotDeleter> snapshot(raw_snapshot);

        std::uint64_t count = 0;
        require_fly(fly_catalog_snapshot_count(snapshot.get(), &count),
                    "fly_catalog_snapshot_count");

        napi_value result = nullptr;
        require_napi(napi_create_array_with_length(env, static_cast<std::size_t>(count), &result),
                     "create catalogSnapshot");
        for (std::uint64_t index = 0; index < count; ++index)
        {
            std::array<char, FLY_CANONICAL_ID_MAX_UTF8_BYTES + 1u> canonical{};
            std::array<char, FLY_CANONICAL_ID_MAX_UTF8_BYTES + 1u> variant{};
            std::array<char, FLY_SCAN_DISPLAY_NAME_MAX_UTF8_BYTES + 1u> display{};
            std::array<char, FLY_SCAN_RELATIVE_PATH_MAX_UTF8_BYTES + 1u> relative{};
            fly_catalog_entry entry{};
            entry.struct_size = FLY_CATALOG_ENTRY_V1_SIZE;
            entry.version = FLY_CATALOG_ENTRY_VERSION_1;
            entry.canonical_id_utf8 = canonical.data();
            entry.canonical_id_capacity = static_cast<std::uint32_t>(canonical.size());
            entry.variant_id_utf8 = variant.data();
            entry.variant_id_capacity = static_cast<std::uint32_t>(variant.size());
            entry.display_name_utf8 = display.data();
            entry.display_name_capacity = static_cast<std::uint32_t>(display.size());
            entry.source_relative_path_utf8 = relative.data();
            entry.source_relative_path_capacity = static_cast<std::uint32_t>(relative.size());
            require_fly(fly_catalog_snapshot_get(snapshot.get(), index, &entry),
                        "fly_catalog_snapshot_get");
            fly_game_title title{};
            require_fly(fly_catalog_snapshot_get_title(snapshot.get(), index, &title),
                        "fly_catalog_snapshot_get_title");
            auto row = row_from_catalog_entry(app, entry);
            flynes::harmony::apply_game_title(row, title);
            row.search_aliases += "\n" + basename_utf8(relative.data());
            require_napi(napi_set_element(env, result, static_cast<std::uint32_t>(index),
                                          make_game_center_row(env, row)),
                         "set catalogSnapshot row");
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
        return report_error(env, "catalogSnapshot failed: unknown native error", false);
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
        return report_error(env, "catalogSnapshot failed: native allocation error", false);
    }
    catch (const std::exception& error)
    {
        return report_error(env, error.what(), false);
    }
    catch (...)
    {
        return report_error(env, "catalogSnapshot failed: unknown native error", false);
    }
}

napi_value CatalogFavoriteSet(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 2;
        napi_value arguments[2] = {nullptr, nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read catalogFavoriteSet arguments");
        if (argument_count < 2)
        {
            throw NapiTypeError("catalogFavoriteSet requires (canonicalId, favorite)");
        }
        const std::string canonical = read_utf8_string(env, arguments[0], "canonicalId");
        const bool favorite = read_bool(env, arguments[1], "favorite");
        fly_app_t& app = require_app();
        require_fly(fly_catalog_favorite_set(&app, canonical.data(),
                                             static_cast<std::uint32_t>(canonical.size()),
                                             favorite ? 1u : 0u),
                    "fly_catalog_favorite_set");
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "catalogFavoriteSet undefined");
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
        return report_error(env, "catalogFavoriteSet failed: unknown native error", false);
    }
}

napi_value CatalogMarkPlayed(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read catalogMarkPlayed arguments");
        if (argument_count < 1)
        {
            throw NapiTypeError("catalogMarkPlayed requires canonicalId");
        }
        const std::string canonical = read_utf8_string(env, arguments[0], "canonicalId");
        fly_app_t& app = require_app();
        require_fly(fly_catalog_mark_played(&app, canonical.data(),
                                            static_cast<std::uint32_t>(canonical.size())),
                    "fly_catalog_mark_played");
        napi_value undefined = nullptr;
        require_napi(napi_get_undefined(env, &undefined), "catalogMarkPlayed undefined");
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
        return report_error(env, "catalogMarkPlayed failed: unknown native error", false);
    }
}

napi_value CatalogUserStateGet(napi_env env, napi_callback_info info)
{
    try
    {
        std::size_t argument_count = 1;
        napi_value arguments[1] = {nullptr};
        require_napi(napi_get_cb_info(
                         env, info, &argument_count, arguments, nullptr, nullptr),
                     "read catalogUserStateGet arguments");
        if (argument_count < 1)
        {
            throw NapiTypeError("catalogUserStateGet requires canonicalId");
        }
        const std::string canonical = read_utf8_string(env, arguments[0], "canonicalId");
        fly_app_t& app = require_app();
        fly_catalog_user_state user{};
        user.struct_size = FLY_CATALOG_USER_STATE_V1_SIZE;
        user.version = FLY_CATALOG_USER_STATE_VERSION_1;
        require_fly(fly_catalog_user_state_get(&app, canonical.data(),
                                               static_cast<std::uint32_t>(canonical.size()),
                                               &user),
                    "fly_catalog_user_state_get");
        napi_value result = nullptr;
        require_napi(napi_create_object(env, &result), "create catalogUserStateGet result");
        require_napi(napi_set_named_property(env, result, "favorite",
                                             create_bool(env, user.favorite != 0, "favorite")),
                     "set favorite");
        require_napi(napi_set_named_property(env, result, "lastPlayedSequence",
                                             create_int64(env, static_cast<std::int64_t>(
                                                 user.last_played_sequence), "lastPlayedSequence")),
                     "set lastPlayedSequence");
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
        return report_error(env, "catalogUserStateGet failed: unknown native error", false);
    }
}

template <typename Operation>
napi_value nearby_call(napi_env env, const char* name, Operation&& operation)
{
    try
    {
        return operation();
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
        const std::string message = std::string(name) + " failed: unknown native error";
        return report_error(env, message.c_str(), false);
    }
}

void nearby_arguments(napi_env env, napi_callback_info info, std::size_t expected,
                      napi_value* arguments, const char* name)
{
    std::size_t argument_count = expected;
    require_napi(napi_get_cb_info(env, info, &argument_count, arguments, nullptr, nullptr),
                 "read nearby invite arguments");
    if (argument_count < expected)
    {
        throw NapiTypeError(std::string(name) + " requires " + std::to_string(expected) +
                            " arguments");
    }
}

napi_value NearbyInviteHostPublish(napi_env env, napi_callback_info info)
{
    return nearby_call(env, "nearbyInviteHostPublish", [&]() {
        napi_value arguments[3] = {nullptr, nullptr, nullptr};
        nearby_arguments(env, info, 3u, arguments, "nearbyInviteHostPublish");
        fly_session_t& session = require_nearby_session();
        const std::uint64_t generation = invite_id(env, arguments[0], "generation");
        const std::string code = read_utf8_string(env, arguments[1], "code");
        const fly_result result = fly_session_invite_host_publish_v1(
            &session, generation, reinterpret_cast<const std::uint8_t*>(code.data()),
            code.size(), uptime_ns(env, arguments[2]));
        if (result == FLY_RESULT_OK)
        {
            // This adapter has changed the displayed generation. A real bearer
            // is still absent, but the shared command must not remain pending.
            resolve_pending_invite_command(session, true);
        }
        return create_bool(env, result == FLY_RESULT_OK, "create host publish result");
    });
}

napi_value NearbyInviteNextHostGeneration(napi_env env, napi_callback_info info)
{
    return nearby_call(env, "nearbyInviteNextHostGeneration", [&]() {
        (void)info;
        if (g_nearby_next_host_generation == 0u ||
            g_nearby_next_host_generation >
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            throw NapiTypeError("nearby host generation exhausted");
        }
        return create_int64(env, static_cast<std::int64_t>(g_nearby_next_host_generation++),
                            "create host generation");
    });
}

napi_value NearbyInviteNextJoinAttemptId(napi_env env, napi_callback_info info)
{
    return nearby_call(env, "nearbyInviteNextJoinAttemptId", [&]() {
        (void)info;
        if (g_nearby_next_join_attempt_id == 0u ||
            g_nearby_next_join_attempt_id >
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            throw NapiTypeError("nearby join attempt id exhausted");
        }
        return create_int64(env, static_cast<std::int64_t>(g_nearby_next_join_attempt_id++),
                            "create join attempt id");
    });
}

napi_value NearbyInviteHostRegenerate(napi_env env, napi_callback_info info)
{
    return nearby_call(env, "nearbyInviteHostRegenerate", [&]() {
        napi_value arguments[3] = {nullptr, nullptr, nullptr};
        nearby_arguments(env, info, 3u, arguments, "nearbyInviteHostRegenerate");
        fly_session_t& session = require_nearby_session();
        const std::uint64_t generation = invite_id(env, arguments[0], "generation");
        const std::string code = read_utf8_string(env, arguments[1], "code");
        const fly_result result = fly_session_invite_host_regenerate_v1(
            &session, generation, reinterpret_cast<const std::uint8_t*>(code.data()),
            code.size(), uptime_ns(env, arguments[2]));
        if (result == FLY_RESULT_OK)
        {
            resolve_pending_invite_command(session, true);
        }
        return create_bool(env, result == FLY_RESULT_OK, "create host regenerate result");
    });
}

napi_value NearbyInviteHostCancel(napi_env env, napi_callback_info info)
{
    return nearby_call(env, "nearbyInviteHostCancel", [&]() {
        napi_value arguments[1] = {nullptr};
        nearby_arguments(env, info, 1u, arguments, "nearbyInviteHostCancel");
        const fly_result result = fly_session_invite_host_cancel_v1(
            &require_nearby_session(), invite_id(env, arguments[0], "generation"));
        return create_bool(env, result == FLY_RESULT_OK, "create host cancel result");
    });
}

napi_value NearbyInviteSubmitCode(napi_env env, napi_callback_info info)
{
    return nearby_call(env, "nearbyInviteSubmitCode", [&]() {
        napi_value arguments[3] = {nullptr, nullptr, nullptr};
        nearby_arguments(env, info, 3u, arguments, "nearbyInviteSubmitCode");
        fly_session_t& session = require_nearby_session();
        const std::string code = read_utf8_string(env, arguments[1], "code");
        const fly_result result = fly_session_invite_submit_code_v1(
            &session, invite_id(env, arguments[0], "attemptId"),
            reinterpret_cast<const std::uint8_t*>(code.data()), code.size(),
            uptime_ns(env, arguments[2]));
        if (result == FLY_RESULT_OK)
        {
            // No Harmony discovery executor exists yet. Fail the real command
            // closed so an unsent request cannot remain live or revive later.
            resolve_pending_invite_command(session, false);
        }
        return create_bool(env, false, "create submit result");
    });
}

napi_value NearbyInviteCancelCode(napi_env env, napi_callback_info info)
{
    return nearby_call(env, "nearbyInviteCancelCode", [&]() {
        napi_value arguments[1] = {nullptr};
        nearby_arguments(env, info, 1u, arguments, "nearbyInviteCancelCode");
        fly_session_t& session = require_nearby_session();
        const fly_result result = fly_session_invite_cancel_code_v1(
            &session, invite_id(env, arguments[0], "attemptId"));
        if (result == FLY_RESULT_OK)
        {
            resolve_pending_invite_command(session, false);
        }
        return create_bool(env, result == FLY_RESULT_OK, "create cancel result");
    });
}

napi_value NearbyInviteTick(napi_env env, napi_callback_info info)
{
    return nearby_call(env, "nearbyInviteTick", [&]() {
        napi_value arguments[1] = {nullptr};
        nearby_arguments(env, info, 1u, arguments, "nearbyInviteTick");
        const fly_result result = fly_session_tick(
            &require_nearby_session(), uptime_ns(env, arguments[0]));
        return create_bool(env, result == FLY_RESULT_OK, "create tick result");
    });
}

napi_value NearbyInviteSnapshot(napi_env env, napi_callback_info info)
{
    return nearby_call(env, "nearbyInviteSnapshot", [&]() {
        (void)info;
        fly_session_invite_snapshot_v1 snapshot{};
        snapshot.struct_size = FLY_SESSION_INVITE_SNAPSHOT_V1_SIZE;
        snapshot.version = FLY_SESSION_INVITE_SNAPSHOT_VERSION_1;
        require_fly(fly_session_get_invite_snapshot(&require_nearby_session(), &snapshot),
                    "fly_session_get_invite_snapshot");
        napi_value result = nullptr;
        require_napi(napi_create_object(env, &result), "create nearby invite snapshot");
        require_napi(napi_set_named_property(
                         env, result, "joinPhase",
                         create_uint32(env, snapshot.join_phase, "joinPhase")),
                     "set joinPhase");
        require_napi(napi_set_named_property(
                         env, result, "hostPhase",
                         create_uint32(env, snapshot.host_phase, "hostPhase")),
                     "set hostPhase");
        require_napi(napi_set_named_property(
                         env, result, "joinAttemptId",
                         create_int64(env, static_cast<std::int64_t>(snapshot.join_attempt_id),
                                      "joinAttemptId")),
                     "set joinAttemptId");
        require_napi(napi_set_named_property(
                         env, result, "hostGeneration",
                         create_int64(env, static_cast<std::int64_t>(snapshot.host_generation),
                                      "hostGeneration")),
                     "set hostGeneration");
        require_napi(napi_set_named_property(
                         env, result, "hostAttemptsLeft",
                         create_uint32(env, snapshot.host_attempts_left, "hostAttemptsLeft")),
                     "set hostAttemptsLeft");
        return result;
    });
}

napi_value Init(napi_env env, napi_value exports)
{
    try
    {
        const napi_property_descriptor descriptors[] = {
            {"catalogSmoke", nullptr, CatalogSmoke, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"catalogSnapshot", nullptr, CatalogSnapshot, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"catalogFavoriteSet", nullptr, CatalogFavoriteSet, nullptr, nullptr, nullptr,
             napi_default, nullptr},
            {"catalogMarkPlayed", nullptr, CatalogMarkPlayed, nullptr, nullptr, nullptr,
             napi_default, nullptr},
            {"catalogUserStateGet", nullptr, CatalogUserStateGet, nullptr, nullptr, nullptr,
             napi_default, nullptr},
            {"nearbyInviteHostPublish", nullptr, NearbyInviteHostPublish, nullptr, nullptr, nullptr,
             napi_default, nullptr},
            {"nearbyInviteNextHostGeneration", nullptr, NearbyInviteNextHostGeneration, nullptr,
             nullptr, nullptr, napi_default, nullptr},
            {"nearbyInviteNextJoinAttemptId", nullptr, NearbyInviteNextJoinAttemptId, nullptr,
             nullptr, nullptr, napi_default, nullptr},
            {"nearbyInviteHostRegenerate", nullptr, NearbyInviteHostRegenerate, nullptr, nullptr,
             nullptr, napi_default, nullptr},
            {"nearbyInviteHostCancel", nullptr, NearbyInviteHostCancel, nullptr, nullptr, nullptr,
             napi_default, nullptr},
            {"nearbyInviteSubmitCode", nullptr, NearbyInviteSubmitCode, nullptr, nullptr, nullptr,
             napi_default, nullptr},
            {"nearbyInviteCancelCode", nullptr, NearbyInviteCancelCode, nullptr, nullptr, nullptr,
             napi_default, nullptr},
            {"nearbyInviteTick", nullptr, NearbyInviteTick, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"nearbyInviteSnapshot", nullptr, NearbyInviteSnapshot, nullptr, nullptr, nullptr,
             napi_default, nullptr},
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
            {"sourceRemove", nullptr, SourceRemove, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"scanBegin", nullptr, ScanBegin, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"scanAddFile", nullptr, ScanAddFile, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"scanCommit", nullptr, ScanCommit, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"scanAbort", nullptr, ScanAbort, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"scanJobStart", nullptr, ScanJobStart, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"scanJobStatus", nullptr, ScanJobStatus, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"scanJobCancel", nullptr, ScanJobCancel, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"playOpen", nullptr, PlayOpen, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playDecodePackage", nullptr, PlayDecodePackage, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"playSetButtons", nullptr, PlaySetButtons, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playStep", nullptr, PlayStep, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playSetPaused", nullptr, PlaySetPaused, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playSetAudioMuted", nullptr, PlaySetAudioMuted, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"playRuntimeStatus", nullptr, PlayRuntimeStatus, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"renderConfigure", nullptr, RenderConfigure, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"renderSetPaused", nullptr, RenderSetPaused, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"renderSetProtection", nullptr, RenderSetProtection, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"renderStatus", nullptr, RenderStatus, nullptr, nullptr, nullptr, napi_default,
             nullptr},
            {"playSaveCheckpoint", nullptr, PlaySaveCheckpoint, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playLoadCheckpoint", nullptr, PlayLoadCheckpoint, nullptr, nullptr, nullptr, napi_default, nullptr},
            {"playClose", nullptr, PlayClose, nullptr, nullptr, nullptr, napi_default, nullptr},
        };
        require_napi(napi_define_properties(
                         env, exports, sizeof(descriptors) / sizeof(descriptors[0]), descriptors),
                     "define entry exports");
        flynes::harmony::harmony_renderer().bind_component(env, exports);
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
