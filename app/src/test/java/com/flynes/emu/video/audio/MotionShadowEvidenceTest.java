package com.flynes.emu.video.audio;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.video.NativePresenterStats;

import org.junit.Test;

public final class MotionShadowEvidenceTest {
    @Test public void derivesStrictArtifactContinuityGpuAndLeaseGates() {
        NativePresenterStats stats = stats(120L, 8_400_000L, 90_000L,
                7L, NativePresenterStats.GPU_TIMING_VALID, 4_200_000L, 9L, 4_800_000L);
        MotionShadowEvidence evidence = MotionShadowEvidence.from(
                stats, 7L, true, 5_000_000L);

        assertEquals(120, evidence.adjacentPairCount());
        assertEquals(0.07, evidence.artifactRatio(), 0.000001);
        assertEquals(0.09, evidence.peakArtifactRatio(), 0.000001);
        assertTrue(evidence.sequencesContinuous());
        assertTrue(evidence.gpuBudgetPass());
        assertTrue(evidence.displayLeaseFresh());
    }

    @Test public void unavailableTimingNewFailureOrStaleLeaseFailsClosed() {
        NativePresenterStats stats = stats(3L, 0L, 0L, 9L,
                NativePresenterStats.GPU_TIMING_PENDING, -1L, 0L, -1L);
        MotionShadowEvidence evidence = MotionShadowEvidence.from(
                stats, 8L, false, 5_000_000L);

        assertFalse(evidence.sequencesContinuous());
        assertFalse(evidence.gpuBudgetPass());
        assertFalse(evidence.displayLeaseFresh());
    }

    @Test public void earlierOverBudgetGpuSampleCannotBeHiddenByFastLastSample() {
        NativePresenterStats stats = stats(120L, 1_000_000L, 20_000L,
                4L, NativePresenterStats.GPU_TIMING_VALID, 3_000_000L,
                8L, 7_000_000L);
        assertFalse(MotionShadowEvidence.from(stats, 4L, true, 5_000_000L)
                .gpuBudgetPass());
    }

    private static NativePresenterStats stats(long pairs, long unsafeSum, long peak,
                                               long failures, int gpuStatus,
                                               long gpuDurationNs, long gpuSamples,
                                               long peakGpuDurationNs) {
        return new NativePresenterStats(0L, 0L, 0L, -1L, 1L, failures, 0,
                NativePresenterStats.GPU_TIMING_UNAVAILABLE, -1L, 0,
                NativePresenterStats.FRAME_RATE_VOTE_CLEARED, -1L,
                0L, 0L, 0L, 0L, 0L, 0L, -1L, 0L, -1L, -1L,
                0L, -1L, -1L, -1L, NativePresenterStats.PACING_OWNER_NATIVE,
                1L, NativePresenterStats.TEMPORAL_PRIMING_SHADOW,
                pairs, unsafeSum, peak, gpuStatus, gpuDurationNs,
                gpuSamples, peakGpuDurationNs);
    }
}
