package com.flynes.emu;

import java.security.SecureRandom;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Local lifecycle of a created invitation (design N01): one active generation,
 * a six-digit code with the leading-zero rule, the 60 s continuous-clock
 * validity, and regenerate/cancel semantics - regeneration and cancellation
 * kill the previous generation immediately, and the old code never comes back.
 *
 * <p>This is UI-side display state bound to one screen. Every lifecycle change
 * must first be accepted by the shared session route through {@link Backend};
 * a locally generated candidate is never displayed as an invitation by itself.
 */
public final class NearbyInviteHostState {
    public static final long VALIDITY_MS = 60_000L;
    public static final int CODE_LENGTH = 6;

    private static final SecureRandom RANDOM = new SecureRandom();
    private static final AtomicLong NEXT_GENERATION = new AtomicLong(1L);

    public interface Backend {
        boolean publish(long generation, String code, long nowMs);
        boolean regenerate(long generation, String code, long nowMs);
        boolean cancel(long generation);
        void tick(long nowMs);
        boolean active(long generation);
    }

    private final Backend backend;

    private long generation;
    private String code = "";
    private long deadlineElapsedRealtime;
    private boolean active;

    public NearbyInviteHostState(Backend backend) {
        if (backend == null) throw new NullPointerException("backend");
        this.backend = backend;
    }

    public boolean create(long nowElapsedRealtime) {
        if (active) return false;
        return activate(nowElapsedRealtime, false);
    }

    public boolean regenerate(long nowElapsedRealtime) {
        if (!active) return false;
        return activate(nowElapsedRealtime, true);
    }

    public boolean cancel(long generation) {
        if (!active || this.generation != generation) return false;
        if (!backend.cancel(generation)) return false;
        active = false;
        code = "";
        return true;
    }

    /** Expires the invitation at/after its deadline; backwards time is refused. */
    public void tick(long nowElapsedRealtime) {
        backend.tick(nowElapsedRealtime);
        if (active && !backend.active(generation)) {
            active = false;
            code = "";
        }
    }

    public boolean active() { return active; }
    public long generation() { return generation; }
    public String code() { return active ? code : ""; }
    public long remainingMs(long nowElapsedRealtime) {
        return active ? Math.max(0L, deadlineElapsedRealtime - nowElapsedRealtime) : 0L;
    }

    private boolean activate(long nowElapsedRealtime, boolean regenerating) {
        long next = NEXT_GENERATION.getAndIncrement();
        if (next == 0L) next = NEXT_GENERATION.getAndIncrement();
        StringBuilder digits = new StringBuilder(CODE_LENGTH);
        for (int i = 0; i < CODE_LENGTH; i++) digits.append(RANDOM.nextInt(10));
        String candidate = digits.toString();
        boolean accepted = regenerating
                ? backend.regenerate(next, candidate, nowElapsedRealtime)
                : backend.publish(next, candidate, nowElapsedRealtime);
        if (!accepted) return false;
        generation = next;
        code = candidate;
        deadlineElapsedRealtime = nowElapsedRealtime + VALIDITY_MS;
        active = true;
        return true;
    }
}
