package com.flynes.emu.video.power;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public final class ThermalPowerMonitorTest {
    @Test public void lifecycleRegistrationIsIdempotentAndHeadroomPollingIsBounded() {
        FakeSource source = new FakeSource();
        ThermalPowerMonitor monitor = new ThermalPowerMonitor(source);

        monitor.start();
        monitor.start();
        assertEquals(1, source.registerCount);

        monitor.pollThermalHeadroom(1_000L);
        monitor.pollThermalHeadroom(10_999L);
        assertEquals(1, source.headroomSampleCount);
        monitor.pollThermalHeadroom(11_000L);
        assertEquals(2, source.headroomSampleCount);

        source.listener.onThermalStatus(ThermalBand.SEVERE);
        assertEquals(ThermalBand.SEVERE, monitor.thermalBand());

        monitor.stop();
        monitor.stop();
        assertEquals(1, source.unregisterCount);
    }

    @Test public void unsupportedHeadroomNeverInventsAHotterOrCoolerBand() {
        FakeSource source = new FakeSource();
        source.sample = null;
        ThermalPowerMonitor monitor = new ThermalPowerMonitor(source);
        monitor.start();
        source.listener.onThermalStatus(ThermalBand.MODERATE);

        monitor.pollThermalHeadroom(1_000L);

        assertEquals(ThermalBand.MODERATE, monitor.thermalBand());
    }

    private static final class FakeSource implements ThermalPowerMonitor.SignalSource {
        int registerCount;
        int unregisterCount;
        int headroomSampleCount;
        ThermalBand sample = ThermalBand.LIGHT;
        ThermalPowerMonitor.Listener listener;

        @Override public void register(ThermalPowerMonitor.Listener listener) {
            registerCount++;
            this.listener = listener;
        }

        @Override public void unregister(ThermalPowerMonitor.Listener listener) {
            unregisterCount++;
        }

        @Override public ThermalBand sampleThermalHeadroom() {
            headroomSampleCount++;
            return sample;
        }
    }
}
