package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

import org.junit.Test;

import java.util.OptionalLong;

public final class InputLatencyTrackerTest {
    @Test public void endpointIsCalibratedCoreSampleNotJavaWriteTime() {
        ClockDomainCalibrator calibrator = new ClockDomainCalibrator();
        calibrator.addPairedSample(100L, 1_000L, 300L); // native 1000 -> Java 200
        InputLatencyTracker tracker = new InputLatencyTracker(calibrator);
        tracker.onTouchGeneration(7L, 150L);

        OptionalLong latency = tracker.onCoreSample(new NativeInputSample(
                7L, new int[]{1, 0, 0, 0}, 1_100L));
        assertEquals(150L, latency.getAsLong());
    }

    @Test public void unknownGenerationOrUncalibratedClockProducesNoMetric() {
        InputLatencyTracker tracker = new InputLatencyTracker(new ClockDomainCalibrator());
        tracker.onTouchGeneration(2L, 10L);
        assertFalse(tracker.onCoreSample(new NativeInputSample(
                2L, new int[4], 20L)).isPresent());
        assertFalse(tracker.onCoreSample(new NativeInputSample(
                3L, new int[4], 20L)).isPresent());
    }
}
