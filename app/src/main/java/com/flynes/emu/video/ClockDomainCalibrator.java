package com.flynes.emu.video;

import java.util.OptionalLong;

/** Maps native steady-clock timestamps into Java elapsedRealtimeNanos via paired JNI calls. */
public final class ClockDomainCalibrator {
    private static final long MAX_UNCERTAINTY_NS = 1_000_000L;
    private long offsetNs;
    private long uncertaintyNs = Long.MAX_VALUE;
    private boolean calibrated;

    public synchronized boolean addPairedSample(long javaBeforeNs, long nativeNs,
                                                long javaAfterNs) {
        if (javaBeforeNs < 0L || nativeNs < 0L || javaAfterNs < javaBeforeNs) return false;
        long roundTrip;
        try {
            roundTrip = Math.subtractExact(javaAfterNs, javaBeforeNs);
        } catch (ArithmeticException overflow) {
            return false;
        }
        long uncertainty = (roundTrip + 1L) / 2L;
        if (uncertainty > MAX_UNCERTAINTY_NS || uncertainty >= uncertaintyNs) return false;
        long midpoint = javaBeforeNs + roundTrip / 2L;
        try {
            offsetNs = Math.subtractExact(midpoint, nativeNs);
        } catch (ArithmeticException overflow) {
            return false;
        }
        uncertaintyNs = uncertainty;
        calibrated = true;
        return true;
    }

    public synchronized OptionalLong toJavaElapsedNs(long nativeNs) {
        if (!calibrated || nativeNs < 0L) return OptionalLong.empty();
        try {
            return OptionalLong.of(Math.addExact(nativeNs, offsetNs));
        } catch (ArithmeticException overflow) {
            return OptionalLong.empty();
        }
    }

    public synchronized long uncertaintyNs() {
        return calibrated ? uncertaintyNs : Long.MAX_VALUE;
    }

    public synchronized void invalidate() {
        calibrated = false;
        uncertaintyNs = Long.MAX_VALUE;
        offsetNs = 0L;
    }
}
