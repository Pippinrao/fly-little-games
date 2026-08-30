package com.flynes.emu.video;

import com.flynes.emu.video.quality.SourceTiming;

import java.util.Objects;

/** One unambiguous core step; audio sample count is data, not an overloaded status code. */
public final class FrameStepResult {
    private final int framesRun;
    private final int audioSamples;
    private final long sequence;
    private final SourceTiming sourceTiming;
    private final int errorCode;

    public FrameStepResult(int framesRun, int audioSamples, long sequence,
                           SourceTiming sourceTiming) {
        if (framesRun < 0 || audioSamples < 0 || (framesRun > 0 && sequence < 0))
            throw new IllegalArgumentException("invalid frame step");
        this.framesRun = framesRun;
        this.audioSamples = audioSamples;
        this.sequence = sequence;
        this.sourceTiming = Objects.requireNonNull(sourceTiming, "sourceTiming");
        this.errorCode = 0;
    }

    private FrameStepResult(int errorCode) {
        this.framesRun = 0;
        this.audioSamples = 0;
        this.sequence = -1L;
        this.sourceTiming = SourceTiming.UNKNOWN;
        this.errorCode = errorCode;
    }

    public static FrameStepResult error(int code) {
        if (code >= 0) throw new IllegalArgumentException("error must be negative");
        return new FrameStepResult(code);
    }

    public int framesRun() { return framesRun; }
    public int audioSamples() { return audioSamples; }
    public long sequence() { return sequence; }
    public SourceTiming sourceTiming() { return sourceTiming; }
    public int errorCode() { return errorCode; }
    public boolean failed() { return errorCode < 0; }
}
