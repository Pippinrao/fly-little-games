package com.flynes.emu.input;

/** Keeps very short virtual-button taps visible to at least one emulator frame. */
public final class MinimumTap {
    private MinimumTap() { }

    public static long remainingMillis(long downTimeMillis, long upTimeMillis,
                                       long minimumDurationMillis) {
        long elapsed = Math.max(0L, upTimeMillis - downTimeMillis);
        return Math.max(0L, minimumDurationMillis - elapsed);
    }
}
