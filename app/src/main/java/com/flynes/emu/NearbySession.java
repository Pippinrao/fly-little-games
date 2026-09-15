package com.flynes.emu;

import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicLong;

/** Process-scoped JNI owner of the shared nearby invitation state machine. */
public final class NearbySession implements NearbyInviteHostState.Backend, AutoCloseable {
    public static final int RESULT_OK = 0;
    public static final int HOST_ACTIVE = 1;

    static { System.loadLibrary("nescore"); }

    private long handle;
    private final AtomicLong nextJoinAttemptId = new AtomicLong(1L);

    private NearbySession(long handle) { this.handle = handle; }

    public static NearbySession create() {
        long[] out = new long[1];
        int result = nativeCreate(out);
        if (result != RESULT_OK || out[0] == 0L) {
            throw new IllegalStateException("fly_session_create failed: " + result);
        }
        return new NearbySession(out[0]);
    }

    @Override public boolean publish(long generation, String code, long nowMs) {
        return nativeHostPublish(handle, generation, ascii(code), toNs(nowMs)) == RESULT_OK
                && nativeResolvePending(handle, true) == RESULT_OK;
    }

    @Override public boolean regenerate(long generation, String code, long nowMs) {
        return nativeHostRegenerate(handle, generation, ascii(code), toNs(nowMs)) == RESULT_OK
                && nativeResolvePending(handle, true) == RESULT_OK;
    }

    @Override public boolean cancel(long generation) {
        return nativeHostCancel(handle, generation) == RESULT_OK;
    }

    @Override public void tick(long nowMs) {
        nativeTick(handle, toNs(nowMs));
    }

    @Override public boolean active(long generation) {
        long[] snapshot = snapshot();
        return snapshot[1] == HOST_ACTIVE && snapshot[3] == generation;
    }

    public boolean submitCode(long attemptId, String code, long nowMs) {
        if (nativeSubmitCode(handle, attemptId, ascii(code), toNs(nowMs)) != RESULT_OK) return false;
        // No Android discovery executor is registered yet. Complete the real
        // shared command as failed so no lookup stays live or can later revive.
        nativeResolvePending(handle, false);
        return false;
    }

    /** Allocates a process-session attempt fence that never resets with an Activity. */
    public long nextJoinAttemptId() {
        long next = nextJoinAttemptId.getAndIncrement();
        if (next <= 0L) throw new IllegalStateException("nearby attempt id exhausted");
        return next;
    }

    public boolean cancelCode(long attemptId) {
        return nativeCancelCode(handle, attemptId) == RESULT_OK;
    }

    /** Cancels a process-scoped host invite whose display owner no longer exists. */
    public void cancelActiveHost() {
        long[] current = snapshot();
        if (current[1] == HOST_ACTIVE && current[3] > 0L) {
            nativeHostCancel(handle, current[3]);
        }
    }

    /** joinPhase, hostPhase, joinAttemptId, hostGeneration, attemptsLeft. */
    public long[] snapshot() {
        long[] out = new long[5];
        int result = nativeSnapshot(handle, out);
        if (result != RESULT_OK) throw new IllegalStateException("invite snapshot failed: " + result);
        return out;
    }

    @Override public void close() {
        if (handle != 0L) {
            nativeDestroy(handle);
            handle = 0L;
        }
    }

    private static byte[] ascii(String value) {
        return (value == null ? "" : value).getBytes(StandardCharsets.US_ASCII);
    }

    private static long toNs(long milliseconds) {
        return Math.multiplyExact(milliseconds, 1_000_000L);
    }

    private static native int nativeCreate(long[] out);
    private static native void nativeDestroy(long handle);
    private static native int nativeHostPublish(long handle, long generation, byte[] code, long nowNs);
    private static native int nativeHostRegenerate(long handle, long generation, byte[] code, long nowNs);
    private static native int nativeHostCancel(long handle, long generation);
    private static native int nativeSubmitCode(long handle, long attemptId, byte[] code, long nowNs);
    private static native int nativeCancelCode(long handle, long attemptId);
    private static native int nativeTick(long handle, long nowNs);
    private static native int nativeSnapshot(long handle, long[] out);
    private static native int nativeResolvePending(long handle, boolean success);
}
