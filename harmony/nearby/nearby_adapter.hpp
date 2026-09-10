#pragma once

#include "friend_store.hpp"

#include <cstddef>
#include <cstdint>

#include <flynes/flynes_session.h>

namespace flynes::harmony::nearby {

enum class NearbyCommandKind : std::uint32_t
{
    None = 0,
    StartDiscovery = 1,
    StopDiscovery = 2,
    ShowQr = 3,
    ConfirmSas = 4,
    CreateBearer = 5,
    OpenQuic = 6,
    PersistFriend = 7,
    StartHostStreamEncoder = 8
};

struct NearbyCommandResult
{
    NearbyCommandKind kind = NearbyCommandKind::None;
    fly_result result = FLY_RESULT_OK;
    fly_result session_result = FLY_RESULT_OK;
    bool submitted_event = false;
};

class NearbyAdapter final
{
public:
    NearbyAdapter(fly_session_t* session, FriendStore* friends);

    NearbyCommandKind map_polled_kind(std::uint32_t session_command_kind) const;
    NearbyCommandResult execute(NearbyCommandKind kind);
    fly_result deliver_stream(std::uint32_t channel, const std::uint8_t* bytes, std::size_t size);
    bool interpreted_seats() const;

private:
    fly_result submit(std::uint32_t event_kind);

    fly_session_t* session_ = nullptr;
    FriendStore* friends_ = nullptr;
    bool interpreted_seats_ = false;
};

} // namespace flynes::harmony::nearby
