package com.flynes.emu;

import java.security.SecureRandom;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Local lifecycle of a created invitation (design N01): one active generation,
 * a six-digit code with the leading-zero rule, the 60 s continuous-clock
 * validity, and regenerate/cancel semantics - regeneration and cancellation
 * kill the previous generation immediately, and the old code never comes back.
 *
 * <p>This is UI-side state bound to one screen; it never announces a pairing,
 * never carries identity data, and is replaced by the shared session route
 * (flynes::session::InviteCodeHost through the ABI) as soon as the backend
 * track wires it. The code locates an invitation only; it is not a credential.
 */
public final class NearbyInviteHostState {
    public static final long VALIDITY_MS = 60_000L;
    public static final int CODE_LENGTH = 6;

    private static final SecureRandom RANDOM = new SecureRandom();
    private static final AtomicLong NEXT_GENERATION = new AtomicLong(1L);

    private long generation;
    private String code = "";
    private long deadlineElapsedRealtime;
    private boolean active;

    public boolean create(long nowElapsedRealtime) {
        if (active) return false;
        return activate(nowElapsedRealtime);
    }

    public boolean regenerate(long nowElapsedRealtime) {
        if (!active) return false;
        return activate(nowElapsedRealtime);
    }

    public boolean cancel(long generation) {
        if (!active || this.generation != generation) return false;
        active = false;
        code = "";
        return true;
    }

    /** Expires the invitation at/after its deadline; backwards time is refused. */
    public void tick(long nowElapsedRealtime) {
        if (active && nowElapsedRealtime >= deadlineElapsedRealtime) {
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

    private boolean activate(long nowElapsedRealtime) {
        long next = NEXT_GENERATION.getAndIncrement();
        if (next == 0L) next = NEXT_GENERATION.getAndIncrement();
        StringBuilder digits = new StringBuilder(CODE_LENGTH);
        for (int i = 0; i < CODE_LENGTH; i++) digits.append(RANDOM.nextInt(10));
        generation = next;
        code = digits.toString();
        deadlineElapsedRealtime = nowElapsedRealtime + VALIDITY_MS;
        active = true;
        return true;
    }
}
