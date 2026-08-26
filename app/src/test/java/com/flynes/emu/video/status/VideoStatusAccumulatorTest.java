package com.flynes.emu.video.status;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import com.flynes.emu.settings.AspectMode;
import com.flynes.emu.video.power.ThermalBand;
import com.flynes.emu.video.quality.DisplayModeCapability;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;
import com.flynes.emu.video.quality.PostEffect;
import com.flynes.emu.video.quality.RuntimeTemporalState;
import com.flynes.emu.video.quality.SourceTiming;
import com.flynes.emu.video.quality.SpatialMode;
import com.flynes.emu.video.quality.TemporalMode;
import com.flynes.emu.video.quality.VideoConfigurationKey;

import org.junit.Test;

public final class VideoStatusAccumulatorTest {
    @Test public void palNativeMetricsNeverAssumeSixtyFramesPerSecond() {
        VideoStatusAccumulator accumulator = new VideoStatusAccumulator();
        accumulator.beginWindow(0L, SourceTiming.PAL_50, 50.0f);
        for (long sequence = 1L; sequence <= 50L; sequence++) {
            accumulator.onCoreFrameProduced(sequence);
            if (sequence <= 45L) {
                accumulator.onSourceFrameCopied(sequence);
                accumulator.onTextureUploaded();
            }
            accumulator.onBufferSubmitted();
        }

        VideoRuntimeStatus status = accumulator.snapshot(1_000L);

        assertEquals(50.0f, status.coreProducedFps(), 0.01f);
        assertEquals(45.0f, status.copiedUniqueSourceFps(), 0.01f);
        assertEquals(45.0f, status.textureUploadFps(), 0.01f);
        assertEquals(5L, status.sourceFramesNotCopied());
        assertEquals(50.0f, status.appBufferSubmitFps(), 0.01f);
        assertEquals(50.0f, status.sourceNominalFps(), 0.01f);
    }

    @Test public void activeIdentityIsPublishedAtomicallyAndClearedDuringTransition() {
        VideoStatusAccumulator accumulator = new VideoStatusAccumulator();
        accumulator.beginWindow(0L, SourceTiming.NTSC_60_0988, 60.0988f);
        VideoConfigurationKey key = new VideoConfigurationKey(SourceTiming.NTSC_60_0988,
                2340, 1080, 1, 60_000, TemporalMode.NATIVE,
                SpatialMode.SHARP_BILINEAR, PostEffect.NONE, AspectMode.FOUR_BY_THREE);

        accumulator.publishStableConfiguration(4L, 7L, "builtin:key", key,
                RuntimeTemporalState.IMMEDIATE_NATIVE, 0, 0.0f);
        VideoRuntimeStatus stable = accumulator.snapshot(100L);
        assertEquals("builtin:key", stable.activeConfigurationId());
        assertEquals(key, stable.activeConfigurationKey());
        assertEquals(4L, stable.surfaceEpoch());
        assertEquals(7L, stable.displayRequestGeneration());

        accumulator.publishTransition(5L, 8L, RuntimeTemporalState.PRIMING, 1, 16.7f);
        VideoRuntimeStatus transitional = accumulator.snapshot(200L);
        assertNull(transitional.activeConfigurationId());
        assertNull(transitional.activeConfigurationKey());
        assertEquals(RuntimeTemporalState.PRIMING, transitional.runtimeTemporalState());
    }

    @Test public void requestedAndSystemReportedModesRemainSeparateFacts() {
        VideoStatusAccumulator accumulator = new VideoStatusAccumulator();
        accumulator.beginWindow(0L, SourceTiming.NTSC_60_0988, 60.0988f);
        DisplayModeCapability requested = new DisplayModeCapability(2, 2340, 1080, 120_000);
        DisplayModeCapability active = new DisplayModeCapability(1, 2340, 1080, 60_000);
        accumulator.onDisplayState(PhysicalRefreshPolicy.HZ_120, requested, active);
        accumulator.onThermalBand(ThermalBand.LIGHT);

        VideoRuntimeStatus status = accumulator.snapshot(100L);

        assertEquals(requested, status.requestedMode());
        assertEquals(active, status.systemReportedActiveMode());
        assertEquals(PhysicalRefreshPolicy.HZ_120, status.requestedPolicy());
        assertEquals(ThermalBand.LIGHT, status.thermalBand());
    }

    @Test public void freshnessIsDerivedAtReadTimeWithHalfOpenLease() {
        assertEquals(StatusFreshness.FRESH,
                StatusFreshness.at(1_000L, 2_499L, 1_500L));
        assertEquals(StatusFreshness.STALE,
                StatusFreshness.at(1_000L, 2_500L, 1_500L));
        assertEquals(StatusFreshness.UNKNOWN,
                StatusFreshness.at(2_000L, 1_999L, 1_500L));
    }
}
