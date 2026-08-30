package com.flynes.emu.cover;

import com.flynes.emu.video.FramePublisher;
import com.flynes.emu.video.PublishedFrame;

import java.util.concurrent.Executor;

/** Samples native game-only frames at 2/4/6/8 seconds and keeps improving the cover. */
public final class CoverCaptureCoordinator implements FramePublisher.Observer {
    public interface Sink { void store(CoverFrame frame); }

    private static final long[] SAMPLE_OFFSETS = {120L, 240L, 360L, 480L};
    private final String canonicalId;
    private final Sink sink;
    private final Executor executor;
    private long firstSequence = Long.MIN_VALUE;
    private int nextSample;
    private double bestScore = Double.NEGATIVE_INFINITY;

    public CoverCaptureCoordinator(String canonicalId, Sink sink, Executor executor) {
        if (canonicalId == null || canonicalId.isBlank() || sink == null || executor == null) {
            throw new IllegalArgumentException("capture requires a game, sink, and executor");
        }
        this.canonicalId = canonicalId;
        this.sink = sink;
        this.executor = executor;
    }

    @Override public synchronized void onFrame(PublishedFrame frame) {
        if (firstSequence == Long.MIN_VALUE) {
            firstSequence = frame.sequence();
            return;
        }
        if (nextSample >= SAMPLE_OFFSETS.length
                || frame.sequence() - firstSequence < SAMPLE_OFFSETS[nextSample]) return;
        nextSample++;
        CoverFrame stable = CoverFrame.copyOf(canonicalId, frame);
        executor.execute(() -> consider(stable));
    }

    private synchronized void consider(CoverFrame frame) {
        double score = FrameQuality.score(frame);
        if (score < FrameQuality.MIN_ACCEPTABLE || score <= bestScore + 1.0) return;
        bestScore = score;
        sink.store(frame);
    }
}
