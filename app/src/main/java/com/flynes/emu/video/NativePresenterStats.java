package com.flynes.emu.video;

/** Monotonic counters reported by the native EGL presentation thread. */
public record NativePresenterStats(long uploadedFrames, long submittedFrames,
                                   long skippedSequences, long lastSequence,
                                   long surfaceEpoch) {
    public static final NativePresenterStats EMPTY =
            new NativePresenterStats(0L, 0L, 0L, -1L, 0L);
}
