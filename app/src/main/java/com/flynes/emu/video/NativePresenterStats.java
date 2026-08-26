package com.flynes.emu.video;

/** Monotonic counters reported by the native EGL presentation thread. */
public record NativePresenterStats(long uploadedFrames, long submittedFrames,
                                   long skippedSequences, long lastSequence,
                                   long surfaceEpoch, long runtimeFailureCount,
                                   int runtimeFailureCode, int gpuTimingStatus,
                                   long lastGpuDurationNs) {
    public static final int GPU_TIMING_UNAVAILABLE = 0;
    public static final int GPU_TIMING_PENDING = 1;
    public static final int GPU_TIMING_VALID = 2;
    public static final NativePresenterStats EMPTY =
            new NativePresenterStats(0L, 0L, 0L, -1L, 0L,
                    0L, 0, GPU_TIMING_UNAVAILABLE, -1L);
}
