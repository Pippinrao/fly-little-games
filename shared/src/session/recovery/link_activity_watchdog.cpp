#include "link_activity_watchdog.hpp"

namespace flynes::session::recovery {

void LinkActivityWatchdogV1::reset(std::uint64_t continuous_ns) noexcept
{
    state_ = LinkActivityStateV1::Live;
    last_activity_ns_ = continuous_ns;
    freeze_ns_ = 0;
    clock_seen_ = true;
}

void LinkActivityWatchdogV1::refresh(std::uint64_t continuous_ns) noexcept
{
    if (state_ == LinkActivityStateV1::ReconnectExpired)
        return;
    last_activity_ns_ = continuous_ns;
    if (state_ == LinkActivityStateV1::Frozen)
        return;
    state_ = LinkActivityStateV1::Live;
    freeze_ns_ = 0;
}

void LinkActivityWatchdogV1::on_verified_peer_activity(
    std::uint64_t continuous_ns) noexcept
{
    refresh(continuous_ns);
}

void LinkActivityWatchdogV1::on_authenticated_peer_ack(
    std::uint64_t continuous_ns) noexcept
{
    refresh(continuous_ns);
}

void LinkActivityWatchdogV1::on_local_send_complete(std::uint64_t) noexcept {}

void LinkActivityWatchdogV1::on_transport_ack(std::uint64_t) noexcept {}

void LinkActivityWatchdogV1::on_queued_callback(std::uint64_t) noexcept {}

LinkActivityStateV1 LinkActivityWatchdogV1::on_clock(
    std::uint64_t continuous_ns) noexcept
{
    if (!clock_seen_)
    {
        last_activity_ns_ = continuous_ns;
        clock_seen_ = true;
        return state_;
    }
    if (state_ == LinkActivityStateV1::Live)
    {
        if (continuous_ns >= last_activity_ns_ &&
            (continuous_ns - last_activity_ns_) >= kLinkFreezeNsV1)
        {
            state_ = LinkActivityStateV1::Frozen;
            freeze_ns_ = continuous_ns;
        }
        return state_;
    }
    if (state_ == LinkActivityStateV1::Frozen)
    {
        if (continuous_ns >= freeze_ns_ &&
            (continuous_ns - freeze_ns_) >= kReconnectDeadlineNsV1)
            state_ = LinkActivityStateV1::ReconnectExpired;
    }
    return state_;
}

} // namespace flynes::session::recovery
