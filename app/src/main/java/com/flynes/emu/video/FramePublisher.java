package com.flynes.emu.video;

import java.util.Optional;
import java.util.concurrent.CopyOnWriteArrayList;

/** Suppresses duplicate, incomplete, and regressing native frame snapshots. */
public final class FramePublisher {
    public interface Source {
        PublishedFrame copyLatest();
    }

    public interface Observer {
        void onFrame(PublishedFrame frame);
    }

    private final Source source;
    private long lastSequence = Long.MIN_VALUE;
    private final CopyOnWriteArrayList<Observer> observers = new CopyOnWriteArrayList<>();

    public FramePublisher(Source source) {
        this.source = source;
    }

    public synchronized Optional<PublishedFrame> poll() {
        PublishedFrame frame = source.copyLatest();
        if (frame == null || !frame.complete() || frame.sequence() <= lastSequence) {
            if (frame != null) frame.close();
            return Optional.empty();
        }
        lastSequence = frame.sequence();
        for (Observer observer : observers) {
            try {
                observer.onFrame(frame);
            } catch (RuntimeException ignored) {
                // Optional consumers such as cover capture must never stop presentation.
            }
        }
        return Optional.of(frame);
    }

    public void addObserver(Observer observer) {
        if (observer == null) throw new IllegalArgumentException("observer must not be null");
        observers.addIfAbsent(observer);
    }

    public void removeObserver(Observer observer) {
        observers.remove(observer);
    }

    public synchronized void reset() {
        lastSequence = Long.MIN_VALUE;
    }
}
