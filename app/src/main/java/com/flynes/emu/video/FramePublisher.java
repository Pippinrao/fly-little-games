package com.flynes.emu.video;

import java.util.Optional;

/** Suppresses duplicate, incomplete, and regressing native frame snapshots. */
public final class FramePublisher {
    public interface Source {
        PublishedFrame copyLatest();
    }

    private final Source source;
    private long lastSequence = Long.MIN_VALUE;

    public FramePublisher(Source source) {
        this.source = source;
    }

    public synchronized Optional<PublishedFrame> poll() {
        PublishedFrame frame = source.copyLatest();
        if (frame == null || !frame.complete() || frame.sequence() <= lastSequence) {
            return Optional.empty();
        }
        lastSequence = frame.sequence();
        return Optional.of(frame);
    }

    public synchronized void reset() {
        lastSequence = Long.MIN_VALUE;
    }
}
