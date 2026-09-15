package com.flynes.emu;

/** Applies the monotonic-clock expiry transition before an invitation is rendered. */
final class NearbyInviteTicker {
    private NearbyInviteTicker() {}

    static void tick(NearbyInviteHostState invite, long nowElapsedRealtime) {
        invite.tick(nowElapsedRealtime);
    }
}
