package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public final class ClockDomainCalibratorTest {
    @Test public void mapsNativeTimeUsingTheLowestUncertaintyPairedSample() {
        ClockDomainCalibrator calibrator = new ClockDomainCalibrator();
        assertTrue(calibrator.addPairedSample(1_000_000L, 10_000_000L, 1_400_000L));
        assertEquals(1_200_000L, calibrator.toJavaElapsedNs(10_000_000L).getAsLong());
        assertEquals(200_000L, calibrator.uncertaintyNs());
    }

    @Test public void rejectsMoreThanOneMillisecondUncertaintyAndTimeAnomalies() {
        ClockDomainCalibrator calibrator = new ClockDomainCalibrator();
        assertFalse(calibrator.addPairedSample(0L, 1L, 2_000_002L));
        assertFalse(calibrator.addPairedSample(5L, 1L, 4L));
        assertFalse(calibrator.toJavaElapsedNs(1L).isPresent());
    }
}
