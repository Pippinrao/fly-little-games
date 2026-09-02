package com.flynes.emu;

/** Consumes direction revisions and rate-limits semantic haptic feedback. */
final class DirectionFeedbackGate {
    private static final long COOLDOWN_MS = 80L;

    private long observedRevision;
    private long lastFeedbackTimeMs;
    private boolean hasFeedbackTime;

    DirectionFeedbackGate(long initialRevision) {
        observedRevision = initialRevision;
    }

    boolean shouldEmit(long revision, long eventTimeMs) {
        if (revision == observedRevision) return false;
        observedRevision = revision;
        if (hasFeedbackTime && eventTimeMs - lastFeedbackTimeMs < COOLDOWN_MS) return false;
        lastFeedbackTimeMs = eventTimeMs;
        hasFeedbackTime = true;
        return true;
    }

    /** Records a terminal-event revision without emitting or changing the cooldown clock. */
    void consume(long revision) {
        observedRevision = revision;
    }

    void reset(long revision) {
        observedRevision = revision;
        hasFeedbackTime = false;
        lastFeedbackTimeMs = 0L;
    }
}
