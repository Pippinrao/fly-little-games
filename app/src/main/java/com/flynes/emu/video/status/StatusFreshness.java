package com.flynes.emu.video.status;

public enum StatusFreshness {
    FRESH,
    STALE,
    UNKNOWN;

    public static StatusFreshness at(long capturedAtElapsedRealtimeMs,
                                     long nowElapsedRealtimeMs,
                                     long ttlMs) {
        if (ttlMs <= 0L) return UNKNOWN;
        try {
            long age = Math.subtractExact(nowElapsedRealtimeMs,
                    capturedAtElapsedRealtimeMs);
            if (age < 0L) return UNKNOWN;
            return age < ttlMs ? FRESH : STALE;
        } catch (ArithmeticException overflow) {
            return UNKNOWN;
        }
    }
}
