package com.flynes.emu.video.power;

import java.util.Objects;

/** Lifecycle-scoped thermal signal monitor with Android's 10-second headroom limit. */
public final class ThermalPowerMonitor {
    public static final long MIN_HEADROOM_SAMPLE_INTERVAL_MS = 10_000L;

    public interface Listener {
        void onThermalStatus(ThermalBand thermalBand);
    }

    public interface SignalSource {
        void register(Listener listener);
        void unregister(Listener listener);
        /** Returns null when the platform cannot provide a reliable sample. */
        ThermalBand sampleThermalHeadroom();
    }

    private final SignalSource source;
    private final Listener listener = this::acceptThermalStatus;
    private boolean started;
    private ThermalBand thermalBand = ThermalBand.NONE;
    private Long lastHeadroomSampleElapsedMs;

    public ThermalPowerMonitor(SignalSource source) {
        this.source = Objects.requireNonNull(source, "source");
    }

    public synchronized void start() {
        if (started) return;
        source.register(listener);
        started = true;
    }

    public synchronized void stop() {
        if (!started) return;
        source.unregister(listener);
        started = false;
        lastHeadroomSampleElapsedMs = null;
    }

    public synchronized void pollThermalHeadroom(long nowElapsedRealtimeMs) {
        if (!started || !headroomSampleDue(nowElapsedRealtimeMs)) return;
        ThermalBand sampled = source.sampleThermalHeadroom();
        lastHeadroomSampleElapsedMs = nowElapsedRealtimeMs;
        if (sampled != null) thermalBand = sampled;
    }

    public synchronized ThermalBand thermalBand() { return thermalBand; }

    private boolean headroomSampleDue(long nowElapsedRealtimeMs) {
        if (lastHeadroomSampleElapsedMs == null) return true;
        try {
            return Math.subtractExact(nowElapsedRealtimeMs,
                    lastHeadroomSampleElapsedMs) >= MIN_HEADROOM_SAMPLE_INTERVAL_MS;
        } catch (ArithmeticException overflow) {
            return false;
        }
    }

    private synchronized void acceptThermalStatus(ThermalBand newBand) {
        if (started && newBand != null) thermalBand = newBand;
    }
}
