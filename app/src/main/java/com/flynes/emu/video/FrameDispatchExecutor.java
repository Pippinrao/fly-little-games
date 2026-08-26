package com.flynes.emu.video;

import java.util.Objects;
import java.util.concurrent.Executor;

/** Native-mode dispatcher: one pending callback, latest sequence wins, skipped work is explicit. */
public final class FrameDispatchExecutor implements AutoCloseable {
    public interface Consumer { void onSequence(long sequence); }

    private final Executor executor;
    private final Consumer consumer;
    private long pending = -1L;
    private long lastDelivered = -1L;
    private long skipped;
    private boolean scheduled;
    private boolean closed;

    public FrameDispatchExecutor(Executor executor, Consumer consumer) {
        this.executor = Objects.requireNonNull(executor, "executor");
        this.consumer = Objects.requireNonNull(consumer, "consumer");
    }

    public synchronized void offer(long sequence) {
        if (closed || sequence < 0L || sequence <= lastDelivered || sequence <= pending) return;
        if (pending >= 0L) skipped += sequence - pending;
        pending = sequence;
        if (scheduled) return;
        scheduled = true;
        executor.execute(this::drain);
    }

    public synchronized long skippedSequences() { return skipped; }

    @Override public synchronized void close() {
        closed = true;
        pending = -1L;
    }

    private void drain() {
        final long sequence;
        synchronized (this) {
            if (closed || pending < 0L) {
                scheduled = false;
                return;
            }
            sequence = pending;
            pending = -1L;
            if (lastDelivered >= 0L && sequence > lastDelivered + 1L)
                skipped += sequence - lastDelivered - 1L;
            lastDelivered = sequence;
        }
        consumer.onSequence(sequence);
        synchronized (this) {
            if (closed || pending < 0L) {
                scheduled = false;
                return;
            }
        }
        executor.execute(this::drain);
    }
}
