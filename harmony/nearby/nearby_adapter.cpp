#include "nearby_adapter.hpp"

namespace flynes::harmony::nearby {
namespace {

fly_session_event make_event(std::uint32_t kind)
{
    fly_session_event event{};
    event.struct_size = FLY_SESSION_EVENT_V1_SIZE;
    event.version = FLY_SESSION_EVENT_VERSION_1;
    event.kind = kind;
    event.reserved = 0;
    return event;
}

} // namespace

NearbyAdapter::NearbyAdapter(fly_session_t* session, FriendStore* friends)
    : session_(session), friends_(friends)
{
}

NearbyCommandKind NearbyAdapter::map_polled_kind(std::uint32_t session_command_kind) const
{
    if (session_command_kind == FLY_SESSION_COMMAND_NONE)
    {
        return NearbyCommandKind::None;
    }
    // Future reducer kinds stay mapped here. They are not BLE/P2P DTOs and are
    // not added to the shared public C ABI in this host-only stub.
    return NearbyCommandKind::None;
}

fly_result NearbyAdapter::submit(std::uint32_t event_kind)
{
    const fly_session_event event = make_event(event_kind);
    return fly_session_submit_event(session_, &event);
}

NearbyCommandResult NearbyAdapter::execute(NearbyCommandKind kind)
{
    NearbyCommandResult result;
    result.kind = kind;

    if (session_ == nullptr)
    {
        result.result = FLY_RESULT_INVALID_ARGUMENT;
        return result;
    }

    if (kind == NearbyCommandKind::StartHostStreamEncoder)
    {
        result.result = FLY_RESULT_UNSUPPORTED_VERSION;
        return result;
    }

    if (kind == NearbyCommandKind::None)
    {
        return result;
    }

    if (kind == NearbyCommandKind::PersistFriend)
    {
        if (friends_ == nullptr)
        {
            result.result = FLY_RESULT_INVALID_ARGUMENT;
            return result;
        }
        FriendRecord record;
        record.contact_id = "host-fake";
        record.identity_key_id.fill(0x5au);
        result.result = friends_->persist(record);
        if (result.result != FLY_RESULT_OK)
        {
            return result;
        }
        result.session_result = submit(FLY_SESSION_EVENT_STORAGE);
        result.submitted_event = true;
        return result;
    }

    const std::uint32_t event_kind =
        (kind == NearbyCommandKind::ConfirmSas || kind == NearbyCommandKind::ShowQr)
            ? FLY_SESSION_EVENT_USER
            : FLY_SESSION_EVENT_TRANSPORT;
    result.session_result = submit(event_kind);
    result.submitted_event = true;
    return result;
}

fly_result NearbyAdapter::deliver_stream(std::uint32_t channel,
                                         const std::uint8_t* bytes,
                                         std::size_t size)
{
    // Forward opaque QUIC bytes. Do not decode ChannelBind, seats, or mode.
    interpreted_seats_ = false;
    return fly_session_receive_stream(session_, channel, bytes, size);
}

bool NearbyAdapter::interpreted_seats() const
{
    return interpreted_seats_;
}

} // namespace flynes::harmony::nearby
