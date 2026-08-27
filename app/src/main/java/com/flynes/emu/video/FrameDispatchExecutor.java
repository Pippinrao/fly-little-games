package com.flynes.emu.video;

import java.util.ArrayDeque;
import java.util.Objects;
import java.util.concurrent.Executor;

/** Dispatches native latest-only frames or a fail-closed, lossless Motion staging sequence. */
public final class FrameDispatchExecutor implements AutoCloseable {
    public interface Consumer { void onSequence(long sequence); }
    public interface FailureListener { void onFailure(Failure failure); }
    public enum Failure { NONE, SOURCE_SEQUENCE_GAP, STAGING_OVERFLOW }
    private enum Policy { NATIVE_LATEST, MOTION_LOSSLESS }

    private final Executor executor;
    private final Consumer consumer;
    private final Policy policy;
    private final int capacity;
    private final FailureListener failureListener;
    private final ArrayDeque<Long> motionQueue = new ArrayDeque<>();
    private long pending = -1L;
    private long lastOffered = -1L;
    private long lastDelivered = -1L;
    private long skipped;
    private boolean scheduled;
    private boolean closed;
    private Failure failure = Failure.NONE;

    /** Native policy: one pending callback, latest sequence wins, skipped work is explicit. */
    public FrameDispatchExecutor(Executor executor, Consumer consumer) {
        this(executor, consumer, Policy.NATIVE_LATEST, 1, failure -> { });
    }

    private FrameDispatchExecutor(Executor executor, Consumer consumer,
                                  Policy policy, int capacity, FailureListener failureListener) {
        this.executor = Objects.requireNonNull(executor, "executor");
        this.consumer = Objects.requireNonNull(consumer, "consumer");
        this.policy = Objects.requireNonNull(policy, "policy");
        this.failureListener = Objects.requireNonNull(failureListener, "failureListener");
        if (capacity <= 0) throw new IllegalArgumentException("capacity must be positive");
        this.capacity = capacity;
    }

    /** Motion policy: every adjacent sequence is retained; gaps and overflow stop dispatch. */
    public static FrameDispatchExecutor motionLossless(Executor executor, Consumer consumer,
                                                       int capacity) {
        return new FrameDispatchExecutor(executor, consumer, Policy.MOTION_LOSSLESS, capacity,
                failure -> { });
    }

    /**
     * Motion capture policy used by production. The consumer runs before offer returns, so it
     * copies the current native framebuffer into the presenter's owned ring before the emulator
     * can publish and overwrite the next frame.
     */
    public static FrameDispatchExecutor motionCaptureSynchronous(Consumer consumer,
                                                                 int capacity) {
        return motionCaptureSynchronous(consumer, capacity, failure -> { });
    }

    public static FrameDispatchExecutor motionCaptureSynchronous(
            Consumer consumer, int capacity, FailureListener failureListener) {
        return new FrameDispatchExecutor(Runnable::run, consumer,
                Policy.MOTION_LOSSLESS, capacity, failureListener);
    }

    public synchronized void offer(long sequence) {
        if (closed || failed() || sequence < 0L) return;
        if (policy == Policy.MOTION_LOSSLESS) offerMotion(sequence);
        else offerNative(sequence);
    }

    private void offerNative(long sequence) {
        if (sequence <= lastDelivered || sequence <= pending) return;
        if (pending >= 0L) skipped += sequence - pending;
        pending = sequence;
        scheduleIfNeeded();
    }

    private void offerMotion(long sequence) {
        if (lastOffered >= 0L && sequence != lastOffered + 1L) {
            fail(Failure.SOURCE_SEQUENCE_GAP);
            return;
        }
        if (motionQueue.size() >= capacity) {
            fail(Failure.STAGING_OVERFLOW);
            return;
        }
        lastOffered = sequence;
        motionQueue.addLast(sequence);
        scheduleIfNeeded();
    }

    private void scheduleIfNeeded() {
        if (scheduled) return;
        scheduled = true;
        executor.execute(this::drain);
    }

    private void fail(Failure reason) {
        failure = reason;
        motionQueue.clear();
        pending = -1L;
        failureListener.onFailure(reason);
    }

    public synchronized void resetMotion() {
        if (policy != Policy.MOTION_LOSSLESS)
            throw new IllegalStateException("only Motion dispatch can reset");
        motionQueue.clear();
        pending = -1L;
        lastOffered = -1L;
        lastDelivered = -1L;
        scheduled = false;
        failure = Failure.NONE;
    }

    public synchronized long skippedSequences() { return skipped; }
    public synchronized boolean failed() { return failure != Failure.NONE; }
    public synchronized Failure failure() { return failure; }
    public synchronized int queuedSequences() {
        return policy == Policy.MOTION_LOSSLESS ? motionQueue.size() : (pending >= 0L ? 1 : 0);
    }

    @Override public synchronized void close() {
        closed = true;
        pending = -1L;
        motionQueue.clear();
    }

    private void drain() {
        final long sequence;
        synchronized (this) {
            if (closed || failed()) {
                scheduled = false;
                return;
            }
            if (policy == Policy.MOTION_LOSSLESS) {
                Long next = motionQueue.pollFirst();
                if (next == null) {
                    scheduled = false;
                    return;
                }
                sequence = next;
            } else {
                if (pending < 0L) {
                    scheduled = false;
                    return;
                }
                sequence = pending;
                pending = -1L;
                if (lastDelivered >= 0L && sequence > lastDelivered + 1L)
                    skipped += sequence - lastDelivered - 1L;
            }
            lastDelivered = sequence;
        }
        consumer.onSequence(sequence);
        synchronized (this) {
            if (closed || failed() || (policy == Policy.MOTION_LOSSLESS
                    ? motionQueue.isEmpty() : pending < 0L)) {
                scheduled = false;
                return;
            }
        }
        executor.execute(this::drain);
    }
}
