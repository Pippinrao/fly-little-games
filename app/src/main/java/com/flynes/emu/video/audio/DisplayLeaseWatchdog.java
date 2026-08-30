package com.flynes.emu.video.audio;

/** Independent absolute-deadline guard checked before synthesis and before swap. */
public final class DisplayLeaseWatchdog {
    public enum Status { FRESH, EXPIRED, STALE_TOKEN, UNARMED }
    public interface Cancellation { void cancel(); }
    public interface DeadlineScheduler {
        Cancellation schedule(Runnable callback, long delayNs);
    }
    public interface ExpiryListener { void onExpired(Token token); }

    public static final class Token {
        private final long serial;
        private final long surfaceEpoch;
        private final long displayGeneration;
        private final long deadlineNs;
        private Token(long serial, long surfaceEpoch, long displayGeneration, long deadlineNs) {
            this.serial = serial;
            this.surfaceEpoch = surfaceEpoch;
            this.displayGeneration = displayGeneration;
            this.deadlineNs = deadlineNs;
        }
        public long surfaceEpoch() { return surfaceEpoch; }
        public long displayGeneration() { return displayGeneration; }
        public long deadlineNs() { return deadlineNs; }
    }

    private final long timeoutNs;
    private final DeadlineScheduler scheduler;
    private final ExpiryListener expiryListener;
    private long serial;
    private boolean armed;
    private Cancellation cancellation;

    public DisplayLeaseWatchdog(long timeoutNs) {
        this(timeoutNs, null, null);
    }

    public DisplayLeaseWatchdog(long timeoutNs, DeadlineScheduler scheduler,
                                ExpiryListener expiryListener) {
        if (timeoutNs <= 0L) throw new IllegalArgumentException("timeout must be positive");
        if ((scheduler == null) != (expiryListener == null))
            throw new IllegalArgumentException("scheduler and listener are paired");
        this.timeoutNs = timeoutNs;
        this.scheduler = scheduler;
        this.expiryListener = expiryListener;
    }

    public synchronized Token arm(long surfaceEpoch, long displayGeneration, long nowNs) {
        if (surfaceEpoch < 0L || displayGeneration < 0L || nowNs < 0L)
            throw new IllegalArgumentException("invalid lease identity");
        ++serial;
        armed = true;
        long deadline;
        try { deadline = Math.addExact(nowNs, timeoutNs); }
        catch (ArithmeticException overflow) { deadline = Long.MAX_VALUE; }
        Token token = new Token(serial, surfaceEpoch, displayGeneration, deadline);
        if (cancellation != null) cancellation.cancel();
        cancellation = scheduler == null ? null
                : scheduler.schedule(() -> expireIfCurrent(token), timeoutNs);
        return token;
    }

    public synchronized Status check(Token token, long nowNs) {
        if (!armed) return Status.UNARMED;
        if (token == null || token.serial != serial) return Status.STALE_TOKEN;
        return nowNs >= token.deadlineNs ? Status.EXPIRED : Status.FRESH;
    }

    public synchronized void clear() {
        armed = false;
        ++serial;
        if (cancellation != null) cancellation.cancel();
        cancellation = null;
    }

    private void expireIfCurrent(Token token) {
        ExpiryListener listener;
        synchronized (this) {
            if (!armed || token.serial != serial) return;
            armed = false;
            cancellation = null;
            listener = expiryListener;
        }
        listener.onExpired(token);
    }
}
