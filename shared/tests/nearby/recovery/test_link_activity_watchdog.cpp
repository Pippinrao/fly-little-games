/*
 * Task 12 / REC-DUAL step 1: 300 ms freeze, 30 s reconnect deadline.
 * Local send, transport ACK and queued callbacks must not refresh activity.
 */

#include "recovery/link_activity_watchdog.hpp"

#include <cstdio>

namespace {

namespace rec = flynes::session::recovery;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void freeze_after_300ms_without_peer_activity()
{
    std::puts("recovery: 300ms freeze");
    rec::LinkActivityWatchdogV1 watch;
    watch.reset(1);
    check(watch.on_clock(1) == rec::LinkActivityStateV1::Live, "starts live");
    watch.on_verified_peer_activity(10);
    check(watch.on_clock(10 + rec::kLinkFreezeNsV1 - 1) == rec::LinkActivityStateV1::Live,
          "T-1ns stays live");
    check(watch.on_clock(10 + rec::kLinkFreezeNsV1) == rec::LinkActivityStateV1::Frozen,
          "300ms without peer activity freezes");
}

void send_and_transport_ack_do_not_extend()
{
    std::puts("recovery: local send/transport ACK do not extend");
    rec::LinkActivityWatchdogV1 watch;
    watch.reset(1);
    watch.on_verified_peer_activity(10);
    watch.on_local_send_complete(10 + rec::kLinkFreezeNsV1);
    watch.on_transport_ack(10 + rec::kLinkFreezeNsV1);
    watch.on_queued_callback(10 + rec::kLinkFreezeNsV1);
    check(watch.on_clock(10 + rec::kLinkFreezeNsV1) == rec::LinkActivityStateV1::Frozen,
          "send complete, transport ACK and queued callbacks do not refresh");
}

void peer_ack_refreshes_and_deadline_is_fixed()
{
    std::puts("recovery: authenticated ACK refreshes; 30s deadline does not slide");
    rec::LinkActivityWatchdogV1 watch;
    watch.reset(1);
    watch.on_verified_peer_activity(10);
    watch.on_authenticated_peer_ack(10 + rec::kLinkFreezeNsV1 - 1);
    check(watch.on_clock(10 + rec::kLinkFreezeNsV1 - 1) == rec::LinkActivityStateV1::Live,
          "authenticated peer ACK refreshes activity");
    check(watch.on_clock(10 + rec::kLinkFreezeNsV1 - 1 + rec::kLinkFreezeNsV1) ==
              rec::LinkActivityStateV1::Frozen,
          "freeze after the refreshed window");
    const auto frozen_at = 10 + rec::kLinkFreezeNsV1 - 1 + rec::kLinkFreezeNsV1;
    watch.on_local_send_complete(frozen_at + rec::kReconnectDeadlineNsV1);
    check(watch.on_clock(frozen_at + rec::kReconnectDeadlineNsV1) ==
              rec::LinkActivityStateV1::ReconnectExpired,
          "30s reconnect deadline does not extend from local send");
}

} // namespace

int main()
{
    freeze_after_300ms_without_peer_activity();
    send_and_transport_ack_do_not_extend();
    peer_ack_refreshes_and_deadline_is_fixed();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d recovery-watchdog checks failed\n", failures);
        return 1;
    }
    std::puts("flynes_link_activity_watchdog passed");
    return 0;
}
