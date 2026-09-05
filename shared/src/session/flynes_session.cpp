#include <flynes/flynes_session.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>

struct fly_session_handle
{
    std::uint64_t last_tick_ns = 0u;
};

namespace {

fly_result validate_config(const fly_session_config* config) noexcept
{
    if (config == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (config->struct_size < FLY_SESSION_CONFIG_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (config->version != FLY_SESSION_CONFIG_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (config->reserved != 0u || config->reserved1 != 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return FLY_RESULT_OK;
}

bool valid_channel(std::uint32_t channel) noexcept
{
    return channel >= FLY_SESSION_QUIC_CONTROL && channel <= FLY_SESSION_QUIC_AUDIO;
}

void fill_genesis(fly_frame_cursor_v1* cursor) noexcept
{
    std::memset(cursor, 0, sizeof(*cursor));
    cursor->struct_size = FLY_FRAME_CURSOR_V1_SIZE;
    cursor->abi_version = FLY_FRAME_CURSOR_VERSION_1;
}

void fill_none_evidence(fly_evidence_cursor_v1* evidence) noexcept
{
    std::memset(evidence, 0, sizeof(*evidence));
    evidence->struct_size = FLY_EVIDENCE_CURSOR_V1_SIZE;
    evidence->version = FLY_EVIDENCE_CURSOR_VERSION_1;
    evidence->kind = FLY_EVIDENCE_CURSOR_NONE;
    fill_genesis(&evidence->cursor);
}

} // namespace

extern "C" fly_result fly_session_create(const fly_session_config* config,
                                         fly_session_t** session_out)
{
    if (session_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    *session_out = nullptr;
    const fly_result validation = validate_config(config);
    if (validation != FLY_RESULT_OK)
    {
        return validation;
    }
    try
    {
        auto session = std::make_unique<fly_session_handle>();
        *session_out = session.release();
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

extern "C" void fly_session_destroy(fly_session_t* session)
{
    delete session;
}

extern "C" fly_result fly_session_submit_event(fly_session_t* session,
                                               const fly_session_event* event)
{
    if (session == nullptr || event == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (event->struct_size < FLY_SESSION_EVENT_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (event->version != FLY_SESSION_EVENT_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (event->kind < FLY_SESSION_EVENT_USER || event->kind > FLY_SESSION_EVENT_TRANSPORT)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (event->reserved != 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_receive_stream(fly_session_t* session,
                                                 uint32_t channel,
                                                 const uint8_t* bytes,
                                                 size_t size)
{
    if (session == nullptr || (bytes == nullptr && size != 0u))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (!valid_channel(channel))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_receive_datagram(fly_session_t* session,
                                                   uint32_t channel,
                                                   const uint8_t* bytes,
                                                   size_t size)
{
    return fly_session_receive_stream(session, channel, bytes, size);
}

extern "C" fly_result fly_session_poll_command(fly_session_t* session,
                                               fly_session_command* command_out)
{
    if (session == nullptr || command_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (command_out->struct_size < FLY_SESSION_COMMAND_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (command_out->version != FLY_SESSION_COMMAND_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    const uint32_t struct_size = command_out->struct_size;
    const uint32_t version = command_out->version;
    std::memset(command_out, 0, sizeof(*command_out));
    command_out->struct_size = struct_size;
    command_out->version = version;
    command_out->kind = FLY_SESSION_COMMAND_NONE;
    return FLY_RESULT_OK;
}

extern "C" fly_result fly_session_complete_command(
    fly_session_t* session,
    const fly_session_command_result* result)
{
    if (session == nullptr || result == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (result->struct_size < FLY_SESSION_COMMAND_RESULT_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (result->version != FLY_SESSION_COMMAND_RESULT_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    return FLY_RESULT_INVALID_STATE;
}

extern "C" fly_result fly_session_tick(fly_session_t* session, uint64_t now_ns)
{
    if (session == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    session->last_tick_ns = now_ns;
    return FLY_RESULT_OK;
}

extern "C" fly_result fly_session_get_snapshot(fly_session_t* session,
                                               fly_session_snapshot* snapshot_out)
{
    if (session == nullptr || snapshot_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (snapshot_out->struct_size < FLY_SESSION_SNAPSHOT_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (snapshot_out->version != FLY_SESSION_SNAPSHOT_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    const uint32_t struct_size = snapshot_out->struct_size;
    const uint32_t version = snapshot_out->version;
    std::memset(snapshot_out, 0, sizeof(*snapshot_out));
    snapshot_out->struct_size = struct_size;
    snapshot_out->version = version;
    snapshot_out->ui_state = FLY_SESSION_UI_IDLE;
    fill_genesis(&snapshot_out->committed_through);
    fill_none_evidence(&snapshot_out->state_verified_through);
    return FLY_RESULT_OK;
}
