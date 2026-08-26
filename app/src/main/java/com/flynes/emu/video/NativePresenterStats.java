package com.flynes.emu.video;

/** Monotonic counters reported by the native EGL presentation thread. */
public record NativePresenterStats(long uploadedFrames, long submittedFrames,
                                   long skippedSequences, long lastSequence,
                                   long surfaceEpoch, long runtimeFailureCount,
                                   int runtimeFailureCode, int gpuTimingStatus,
                                   long lastGpuDurationNs,
                                   int requestedFrameRateMilliHz,
                                   int frameRateVoteStatus) {
    public static final int GPU_TIMING_UNAVAILABLE = 0;
    public static final int GPU_TIMING_PENDING = 1;
    public static final int GPU_TIMING_VALID = 2;
    public static final int FRAME_RATE_VOTE_CLEARED = 0;
    public static final int FRAME_RATE_VOTE_APPLIED = 1;
    public static final int FRAME_RATE_VOTE_UNSUPPORTED = 2;
    public static final int FRAME_RATE_VOTE_FAILED = 3;
    public static final int FRAME_RATE_VOTE_STALE_EPOCH = 4;
    public static final NativePresenterStats EMPTY =
            new NativePresenterStats(0L, 0L, 0L, -1L, 0L,
                    0L, 0, GPU_TIMING_UNAVAILABLE, -1L,
                    0, FRAME_RATE_VOTE_CLEARED);
}
