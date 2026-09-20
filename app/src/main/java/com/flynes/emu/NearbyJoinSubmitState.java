package com.flynes.emu;

/**
 * Joiner submit fence: one in-flight attempt, restore after failure so the same
 * page can edit/retry/cancel. Double-submit while in flight does not send again.
 */
public final class NearbyJoinSubmitState {
    private boolean inFlight;
    private boolean failed;
    private int sendCount;

    public boolean submitEnabled(String raw) {
        return !inFlight && NearbyInviteCode.normalize(raw) != null;
    }

    public boolean inFlight() {
        return inFlight;
    }

    public boolean cancelVisible() {
        return inFlight || failed;
    }

    public boolean inputLocked() {
        return inFlight;
    }

    public int sendCount() {
        return sendCount;
    }

    /** @return true when a lookup request should be issued */
    public boolean submit(String raw) {
        if (inFlight) return false;
        if (NearbyInviteCode.normalize(raw) == null) return false;
        inFlight = true;
        failed = false;
        sendCount++;
        return true;
    }

    public void onFailure() {
        inFlight = false;
        failed = true;
    }

    public void cancel() {
        inFlight = false;
        failed = false;
    }
}
