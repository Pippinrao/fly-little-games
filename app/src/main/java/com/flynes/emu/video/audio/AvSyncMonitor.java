package com.flynes.emu.video.audio;

import com.flynes.emu.video.ClockDomainCalibrator;

import java.util.Objects;
import java.util.OptionalLong;

/** A/V gate based only on actual playback/presentation timestamps in one clock domain. */
public final class AvSyncMonitor {
    public static final class Sample {
        private final boolean valid;
        private final long skewNs;
        private final long uncertaintyNs;
        private final long sampledAtNs;
        private Sample(boolean valid, long skewNs, long uncertaintyNs, long sampledAtNs) {
            this.valid = valid;
            this.skewNs = skewNs;
            this.uncertaintyNs = uncertaintyNs;
            this.sampledAtNs = sampledAtNs;
        }
        public boolean valid() { return valid; }
        public long skewNs() { return skewNs; }
        public long uncertaintyNs() { return uncertaintyNs; }
        public long sampledAtNs() { return sampledAtNs; }
    }

    private final ClockDomainCalibrator calibrator;
    private Sample latest = new Sample(false, 0L, Long.MAX_VALUE, Long.MIN_VALUE);

    public synchronized void invalidate() {
        latest = new Sample(false, 0L, Long.MAX_VALUE, System.nanoTime());
    }

    public AvSyncMonitor(ClockDomainCalibrator calibrator) {
        this.calibrator = Objects.requireNonNull(calibrator, "calibrator");
    }

    public synchronized Sample record(long audioPlaybackElapsedNs,
                                      long presenterNativeNs,
                                      long audioTimestampUncertaintyNs) {
        if (audioPlaybackElapsedNs < 0L || presenterNativeNs < 0L
                || audioTimestampUncertaintyNs < 0L) {
            invalidate();
            return latest;
        }
        OptionalLong mapped = calibrator.toJavaElapsedNs(presenterNativeNs);
        if (!mapped.isPresent()) {
            invalidate();
            return latest;
        }
        try {
            long skew = Math.subtractExact(audioPlaybackElapsedNs, mapped.getAsLong());
            long uncertainty = Math.addExact(calibrator.uncertaintyNs(),
                    audioTimestampUncertaintyNs);
            latest = new Sample(true, skew, uncertainty, System.nanoTime());
        } catch (ArithmeticException overflow) {
            invalidate();
        }
        return latest;
    }

    /** Compares only timestamps belonging to the same emulated content frame. */
    public synchronized Sample recordMatched(long audioContentSequence,
                                             long audioPresentationMonotonicNs,
                                             long videoContentSequence,
                                             long videoPresentationMonotonicNs,
                                             long uncertaintyNs) {
        if (audioContentSequence < 0L || audioContentSequence != videoContentSequence
                || audioPresentationMonotonicNs < 0L || videoPresentationMonotonicNs < 0L
                || uncertaintyNs < 0L) {
            invalidate();
            return latest;
        }
        try {
            latest = new Sample(true, Math.subtractExact(audioPresentationMonotonicNs,
                    videoPresentationMonotonicNs), uncertaintyNs, System.nanoTime());
        } catch (ArithmeticException overflow) {
            invalidate();
        }
        return latest;
    }

    public synchronized Sample latest() { return latest; }
    public synchronized Sample latestFresh(long nowNs, long maximumAgeNs) {
        if (!latest.valid || nowNs < latest.sampledAtNs
                || nowNs - latest.sampledAtNs > maximumAgeNs) {
            return new Sample(false, 0L, Long.MAX_VALUE, nowNs);
        }
        return latest;
    }
}
