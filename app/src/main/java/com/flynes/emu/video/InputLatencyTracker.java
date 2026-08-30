package com.flynes.emu.video;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.OptionalLong;

/** Correlates a touch generation with the calibrated time the core actually sampled it. */
public final class InputLatencyTracker {
    private static final int MAX_PENDING = 128;
    private final ClockDomainCalibrator calibrator;
    private final LinkedHashMap<Long, Long> touchTimes = new LinkedHashMap<>();

    public InputLatencyTracker(ClockDomainCalibrator calibrator) {
        if (calibrator == null) throw new IllegalArgumentException("calibrator is required");
        this.calibrator = calibrator;
    }

    public synchronized void onTouchGeneration(long generation, long javaTouchElapsedNs) {
        if (generation <= 0L || javaTouchElapsedNs < 0L) return;
        touchTimes.put(generation, javaTouchElapsedNs);
        while (touchTimes.size() > MAX_PENDING) {
            Long oldest = touchTimes.keySet().iterator().next();
            touchTimes.remove(oldest);
        }
    }

    public synchronized OptionalLong onCoreSample(NativeInputSample sample) {
        if (sample == null) return OptionalLong.empty();
        Long touch = touchTimes.remove(sample.generation());
        if (touch == null) return OptionalLong.empty();
        OptionalLong coreJava = calibrator.toJavaElapsedNs(sample.nativeMonotonicNs());
        if (!coreJava.isPresent() || coreJava.getAsLong() < touch) return OptionalLong.empty();
        return OptionalLong.of(coreJava.getAsLong() - touch);
    }

    public synchronized void clear() { touchTimes.clear(); }
}
