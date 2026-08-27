package com.flynes.emu.video.audio;

import com.flynes.emu.video.NativePresenterStats;

/** Immutable, fail-closed evidence projected from one non-presented Shadow sample. */
public record MotionShadowEvidence(int adjacentPairCount, double artifactRatio,
                                   double peakArtifactRatio, boolean sequencesContinuous,
                                   boolean gpuBudgetPass, boolean displayLeaseFresh) {
    public static MotionShadowEvidence from(NativePresenterStats stats,
                                            long runtimeFailureBaseline,
                                            boolean displayLeaseFresh,
                                            long gpuBudgetNs) {
        if (stats == null || gpuBudgetNs <= 0L) {
            throw new IllegalArgumentException("stats and positive GPU budget are required");
        }
        long pairs = Math.max(0L, stats.shadowPairCount());
        int boundedPairs = (int) Math.min(Integer.MAX_VALUE, pairs);
        double artifact = pairs == 0L ? 0.0
                : (double) stats.shadowUnsafePpmSum() / (double) pairs / 1_000_000.0;
        double peak = Math.max(0L, stats.shadowPeakUnsafePpm()) / 1_000_000.0;
        boolean gpuPass = stats.shadowGpuValidSampleCount() > 0L
                && stats.shadowPeakGpuDurationNs() > 0L
                && stats.shadowPeakGpuDurationNs() <= gpuBudgetNs;
        return new MotionShadowEvidence(boundedPairs, artifact, peak,
                stats.runtimeFailureCount() == runtimeFailureBaseline,
                gpuPass, displayLeaseFresh);
    }
}
