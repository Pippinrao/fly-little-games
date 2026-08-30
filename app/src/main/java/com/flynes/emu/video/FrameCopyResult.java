package com.flynes.emu.video;

import com.flynes.emu.video.quality.SourceTiming;

import java.util.Objects;

/** Separates a native ABI status from frame identity; sequence values are never errors. */
public final class FrameCopyResult {
    public enum Kind { NEW, NO_CHANGE, ERROR }

    private final Kind kind;
    private final long sequence;
    private final long nativeMonotonicNs;
    private final SourceTiming sourceTiming;
    private final int errorCode;

    private FrameCopyResult(Kind kind, long sequence, long nativeMonotonicNs,
                            SourceTiming sourceTiming, int errorCode) {
        this.kind = kind;
        this.sequence = sequence;
        this.nativeMonotonicNs = nativeMonotonicNs;
        this.sourceTiming = sourceTiming;
        this.errorCode = errorCode;
    }

    public static FrameCopyResult newFrame(long sequence, long nativeMonotonicNs,
                                           SourceTiming timing) {
        if (sequence < 0L || nativeMonotonicNs <= 0L)
            throw new IllegalArgumentException("invalid native frame identity");
        return new FrameCopyResult(Kind.NEW, sequence, nativeMonotonicNs,
                Objects.requireNonNull(timing, "timing"), 0);
    }

    public static FrameCopyResult noChange() {
        return new FrameCopyResult(Kind.NO_CHANGE, -1L, 0L, SourceTiming.UNKNOWN, 0);
    }

    public static FrameCopyResult error(int errorCode) {
        if (errorCode >= 0) throw new IllegalArgumentException("error code must be negative");
        return new FrameCopyResult(Kind.ERROR, -1L, 0L, SourceTiming.UNKNOWN, errorCode);
    }

    public Kind kind() { return kind; }
    public long sequence() { return sequence; }
    public long nativeMonotonicNs() { return nativeMonotonicNs; }
    public SourceTiming sourceTiming() { return sourceTiming; }
    public int errorCode() { return errorCode; }
}
