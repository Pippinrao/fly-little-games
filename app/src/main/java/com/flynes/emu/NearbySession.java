package com.flynes.emu;

import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Process-scoped invite facade over the same V2 {@link NearbySessionOwner}.
 * Host publish/cancel map to CREATE_INVITE / CANCEL_INVITE; join-by-code maps
 * to JOIN_CODE. There is no second fly_session_create (V1) invite owner.
 */
public final class NearbySession implements NearbyInviteHostState.Backend, AutoCloseable {
    public static final int RESULT_OK = 0;
    public static final int RESULT_ACCEPTED = 1;
    public static final int HOST_ACTIVE = 1;

    private static final int ACTION_CREATE_INVITE = 5;
    private static final int ACTION_CANCEL_INVITE = 7;
    private static final int ACTION_CANCEL_JOIN = 8;
    private static final int ACTION_JOIN_CODE = 9;
    private static final int LINK_JOINING = 4;

    private final NearbySessionOwner owner;
    private final AtomicLong nextJoinAttemptId = new AtomicLong(1L);
    private long hostGeneration;
    private long joinAttemptId;

    private NearbySession(NearbySessionOwner owner) {
        this.owner = owner;
    }

    public static NearbySession attach(NearbySessionOwner owner) {
        if (owner == null) throw new NullPointerException("owner");
        return new NearbySession(owner);
    }

    /** Nearby entry after create failure: pages keep working and show an unavailable reason. */
    public static NearbySession unavailable() {
        return new NearbySession(null);
    }

    @Override public boolean publish(long generation, String code, long nowMs) {
        if (owner == null) return false;
        if (!accepted(owner.submitAction(ACTION_CREATE_INVITE, null))) return false;
        hostGeneration = generation;
        return true;
    }

    @Override public boolean regenerate(long generation, String code, long nowMs) {
        if (owner == null) return false;
        // REGENERATE_INVITE_V2 is not reduced; cancel then create.
        if (hostGeneration != 0L) {
            owner.submitAction(ACTION_CANCEL_INVITE, null);
        }
        if (!accepted(owner.submitAction(ACTION_CREATE_INVITE, null))) return false;
        hostGeneration = generation;
        return true;
    }

    @Override public boolean cancel(long generation) {
        if (owner == null) {
            if (hostGeneration == generation) hostGeneration = 0L;
            return true;
        }
        NearbySessionOwner.Snapshot snap = owner.snapshot();
        if (snap.linkState != NearbySessionOwner.LINK_INVITING) {
            if (hostGeneration == generation) hostGeneration = 0L;
            return true;
        }
        if (!accepted(owner.submitAction(ACTION_CANCEL_INVITE, null))) return false;
        if (hostGeneration == generation) hostGeneration = 0L;
        return true;
    }

    @Override public void tick(long nowMs) {
        if (owner != null) owner.snapshot();
    }

    @Override public boolean active(long generation) {
        return owner != null
                && owner.snapshot().linkState == NearbySessionOwner.LINK_INVITING
                && hostGeneration == generation;
    }

    public boolean submitCode(long attemptId, String code, long nowMs) {
        if (owner == null) return false;
        owner.submitAction(ACTION_JOIN_CODE, ascii(code));
        joinAttemptId = attemptId;
        // Scan stays UNAVAILABLE on emulator: this is not a wireless peer.
        return owner.snapshot().linkState == LINK_JOINING;
    }

    /** Allocates a process-session attempt fence that never resets with an Activity. */
    public long nextJoinAttemptId() {
        long next = nextJoinAttemptId.getAndIncrement();
        if (next <= 0L) throw new IllegalStateException("nearby attempt id exhausted");
        return next;
    }

    public boolean cancelCode(long attemptId) {
        if (owner != null) owner.submitAction(ACTION_CANCEL_JOIN, null);
        if (joinAttemptId == attemptId) joinAttemptId = 0L;
        return true;
    }

    /** Cancels a process-scoped host invite whose display owner no longer exists. */
    public void cancelActiveHost() {
        long[] current = snapshot();
        if (current[1] == HOST_ACTIVE && current[3] > 0L) {
            cancel(current[3]);
        }
    }

    /** joinPhase, hostPhase, joinAttemptId, hostGeneration, attemptsLeft. */
    public long[] snapshot() {
        if (owner == null) {
            return new long[] { 0L, 0L, joinAttemptId, hostGeneration, 0L };
        }
        NearbySessionOwner.Snapshot snap = owner.snapshot();
        long hostPhase = snap.linkState == NearbySessionOwner.LINK_INVITING ? HOST_ACTIVE : 0L;
        return new long[] { 0L, hostPhase, joinAttemptId, hostGeneration, 0L };
    }

    @Override public void close() {
        // The V2 owner is process-scoped; this facade must not destroy it.
    }

    private static boolean accepted(int result) {
        return result == RESULT_OK || result == RESULT_ACCEPTED;
    }

    private static byte[] ascii(String value) {
        return (value == null ? "" : value).getBytes(StandardCharsets.US_ASCII);
    }
}
