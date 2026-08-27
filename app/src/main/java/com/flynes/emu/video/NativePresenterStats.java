package com.flynes.emu.video;

/** Monotonic counters reported by the native EGL presentation thread. */
public record NativePresenterStats(long uploadedFrames, long submittedFrames,
                                   long skippedSequences, long lastSequence,
                                   long surfaceEpoch, long runtimeFailureCount,
                                   int runtimeFailureCode, int gpuTimingStatus,
                                   long lastGpuDurationNs,
                                   int requestedFrameRateMilliHz,
                                   int frameRateVoteStatus,
                                   long lastPresentationNs,
                                   long realSlots, long interpolatedSlots,
                                   long heldSlots, long warpedSlots,
                                   long cadenceAdjustments, long swappyTotalFrames,
                                   long swappyLastProgressNs, long motionQueueDepth,
                                   long lastPairASequence, long lastPairBSequence,
                                   long actualPresentationCount,
                                   long lastActualPresentationNs,
                                   long lastActualRealSequence,
                                   long lastActualRealPresentationNs,
                                   int pacingOwner,
                                   long lastTransitionId,
                                   int temporalState,
                                   long shadowPairCount,
                                   long shadowUnsafePpmSum,
                                   long shadowPeakUnsafePpm,
                                   int shadowGpuTimingStatus,
                                   long shadowLastGpuDurationNs,
                                   long shadowGpuValidSampleCount,
                                   long shadowPeakGpuDurationNs) {
    public static final int PACING_OWNER_NONE = 0;
    public static final int PACING_OWNER_NATIVE = 1;
    public static final int PACING_OWNER_MOTION = 2;
    public static final int TEMPORAL_IMMEDIATE_NATIVE = 0;
    public static final int TEMPORAL_MOTION = 2;
    public static final int TEMPORAL_BUFFERED_HOLD = 3;
    public static final int TEMPORAL_PRIMING_SHADOW = 4;
    public static final int TEMPORAL_SURFACE_SUSPENDED_HOLD = 6;
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
                    0, FRAME_RATE_VOTE_CLEARED, -1L,
                    0L, 0L, 0L, 0L, 0L, 0L, -1L, 0L, -1L, -1L,
                    0L, -1L, -1L, -1L, PACING_OWNER_NONE, 0L, TEMPORAL_IMMEDIATE_NATIVE,
                    0L, 0L, 0L, GPU_TIMING_UNAVAILABLE, -1L, 0L, -1L);
}
