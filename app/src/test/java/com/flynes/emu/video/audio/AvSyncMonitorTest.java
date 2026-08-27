package com.flynes.emu.video.audio;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.video.ClockDomainCalibrator;

import org.junit.Test;

public final class AvSyncMonitorTest {
    @Test public void usesMappedPlaybackAndPresentationTimestampsNotQueueDepth() {
        ClockDomainCalibrator calibrator = new ClockDomainCalibrator();
        assertTrue(calibrator.addPairedSample(1_000_000L, 10_000_000L, 1_200_000L));
        AvSyncMonitor monitor = new AvSyncMonitor(calibrator);
        AvSyncMonitor.Sample sample = monitor.record(1_600_000L, 10_400_000L, 50_000L);
        assertTrue(sample.valid());
        assertEquals(100_000L, sample.skewNs());
        assertEquals(150_000L, sample.uncertaintyNs());
    }

    @Test public void uncalibratedOrNegativeTimestampsFailClosed() {
        AvSyncMonitor monitor = new AvSyncMonitor(new ClockDomainCalibrator());
        assertFalse(monitor.record(1L, 1L, 0L).valid());
        assertFalse(monitor.record(-1L, 1L, 0L).valid());
    }

    @Test public void contentGateRejectsDifferentAudioAndVideoFrames() {
        AvSyncMonitor monitor = new AvSyncMonitor(new ClockDomainCalibrator());
        assertFalse(monitor.recordMatched(10L, 1_000L, 11L, 900L, 20L).valid());
        AvSyncMonitor.Sample matched = monitor.recordMatched(
                10L, 1_000L, 10L, 900L, 20L);
        assertTrue(matched.valid());
        assertEquals(100L, matched.skewNs());
        assertEquals(20L, matched.uncertaintyNs());
    }

    @Test public void oldOrExplicitlyInvalidatedSampleFailsClosed() {
        ClockDomainCalibrator calibrator = new ClockDomainCalibrator();
        AvSyncMonitor monitor = new AvSyncMonitor(calibrator);
        AvSyncMonitor.Sample sample = monitor.recordMatched(
                7L, 2_000L, 7L, 1_900L, 20L);
        assertTrue(sample.valid());
        assertFalse(monitor.latestFresh(sample.sampledAtNs() + 101L, 100L).valid());

        monitor.recordMatched(8L, 3_000L, 8L, 2_900L, 30L);
        monitor.invalidate();
        assertFalse(monitor.latest().valid());
    }
}
