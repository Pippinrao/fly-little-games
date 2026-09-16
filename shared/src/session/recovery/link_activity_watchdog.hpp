#ifndef FLYNES_SESSION_RECOVERY_LINK_ACTIVITY_WATCHDOG_HPP
#define FLYNES_SESSION_RECOVERY_LINK_ACTIVITY_WATCHDOG_HPP

/*
 * Task 12 / REC-DUAL step 1: 300 ms freeze and 30 s reconnect deadline.
 *
 * Only verified inbound peer activity, or an authenticated ACK that the peer
 * received a local message, may refresh activity. Local send completion,
 * transport ACKs and queued callbacks must not extend either timer.
 */

#include "flynes/flynes_session.h"

#include <cstdint>

namespace flynes::session::recovery {

inline constexpr std::uint64_t kLinkFreezeNsV1 = 300ull * 1000ull * 1000ull;
inline constexpr std::uint64_t kReconnectDeadlineNsV1 =
    30ull * 1000ull * 1000ull * 1000ull;

enum class LinkActivityStateV1 : std::uint8_t
{
    Live = 1,
    Frozen = 2,
    ReconnectExpired = 3
};

class LinkActivityWatchdogV1 final
{
public:
    void reset(std::uint64_t continuous_ns) noexcept;
    void on_verified_peer_activity(std::uint64_t continuous_ns) noexcept;
    void on_authenticated_peer_ack(std::uint64_t continuous_ns) noexcept;
    void on_local_send_complete(std::uint64_t continuous_ns) noexcept;
    void on_transport_ack(std::uint64_t continuous_ns) noexcept;
    void on_queued_callback(std::uint64_t continuous_ns) noexcept;
    LinkActivityStateV1 on_clock(std::uint64_t continuous_ns) noexcept;

    [[nodiscard]] LinkActivityStateV1 state() const noexcept { return state_; }
    [[nodiscard]] bool frozen() const noexcept
    {
        return state_ != LinkActivityStateV1::Live;
    }
    [[nodiscard]] bool reconnect_expired() const noexcept
    {
        return state_ == LinkActivityStateV1::ReconnectExpired;
    }

private:
    void refresh(std::uint64_t continuous_ns) noexcept;

    LinkActivityStateV1 state_ = LinkActivityStateV1::Live;
    std::uint64_t last_activity_ns_ = 0;
    std::uint64_t freeze_ns_ = 0;
    bool clock_seen_ = false;
};

} // namespace flynes::session::recovery

#endif
