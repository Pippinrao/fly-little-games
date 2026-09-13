package com.flynes.emu;

/**
 * Pure projection of the nearby entry status (UI contract nearby_ui_v1,
 * design 2026-09-13 §3.1 / U02). Mirrors shared
 * flynes::product::nearby::project_entry; the connection facts come from real
 * session state only - never from elapsed time, page paths, test flags or
 * persisted booleans (scan success, host acceptance, a correct code and a
 * single-side SAS confirmation can never produce 双人联机中 on their own).
 *
 * <p>This build has no end-to-end pairing flow yet, so {@link NearbyFacts#NONE}
 * (a known, clean disconnected state) is what the home screen reads; the
 * backend track replaces it with the live ABI snapshot without touching this
 * projection.
 */
public final class NearbyEntryStatus {
    private NearbyEntryStatus() { }

    /** Real session facts. All fields default to "known and clean". */
    public static final class NearbyFacts {
        public boolean factsKnown = true;
        public boolean peerVerified;
        public boolean channelBound;
        public boolean compatibilityVerified;
        public boolean connectionEstablished;
        public boolean previousConnectionEstablished;
        public boolean reconnecting;

        /** The state of a build whose session layer reports no pairing flow. */
        public static final NearbyFacts NONE = new NearbyFacts();
    }

    public enum Status { UNKNOWN, DISCONNECTED, PAIRING, CONNECTED, INTERRUPTED }

    public static final class Entry {
        public final Status status;
        public final String stringKey;
        public final String text;
        Entry(Status status, String stringKey, String text) {
            this.status = status;
            this.stringKey = stringKey;
            this.text = text;
        }
    }

    public static Entry project(NearbyFacts facts) {
        if (facts == null || !facts.factsKnown) {
            return new Entry(Status.UNKNOWN, "nearby.entry.unavailable",
                    "暂时无法确认联机状态");
        }
        if (facts.connectionEstablished && facts.peerVerified && facts.channelBound
                && facts.compatibilityVerified) {
            return new Entry(Status.CONNECTED, "nearby.entry.connected", "双人联机中");
        }
        if (facts.previousConnectionEstablished && facts.reconnecting) {
            return new Entry(Status.INTERRUPTED, "nearby.entry.interrupted", "联机中断");
        }
        boolean pairing = facts.peerVerified || facts.channelBound
                || facts.compatibilityVerified || facts.reconnecting;
        return new Entry(pairing ? Status.PAIRING : Status.DISCONNECTED,
                "nearby.open", "附近联机");
    }
}
