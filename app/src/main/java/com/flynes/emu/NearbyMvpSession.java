package com.flynes.emu;

import java.security.SecureRandom;
import java.util.Arrays;

/** One native LAN session for the foreground MVP pairing page. */
final class NearbyMvpSession implements AutoCloseable {
    static final int INVITING = 1;
    static final int LOBBY = 3;
    static final int ENDED = 4;
    static final int CONFIGURING = 5;
    static final int RUNNING = 6;
    static final int RETURNING = 7;

    static { System.loadLibrary("nescore"); }

    private long handle;

    NearbyMvpSession() {
        handle = nativeCreate();
        if (handle == 0) throw new IllegalStateException("Nearby LAN transport unavailable");
    }

    synchronized boolean host(String ipv4) {
        if (handle == 0 || ipv4 == null) return false;
        byte[] token = new byte[16];
        new SecureRandom().nextBytes(token);
        try {
            return nativeHost(handle, ipv4, token);
        } finally {
            Arrays.fill(token, (byte) 0);
        }
    }

    synchronized String invite() {
        return handle == 0 ? null : nativeInvite(handle);
    }

    synchronized int[] snapshot() {
        return handle == 0 ? new int[]{ENDED, 5} : nativeSnapshot(handle);
    }

    synchronized byte[] sessionId() {
        return handle == 0 ? null : nativeSessionId(handle);
    }

    synchronized boolean selectRom(byte[] rom) {
        return handle != 0 && rom != null && nativeSelectRom(handle, rom);
    }
    synchronized boolean selectGame(byte[] rom, String key) {
        return handle != 0 && rom != null && nativeSelectGame(handle, rom, key);
    }

    synchronized boolean confirm() { return handle != 0 && nativeConfirm(handle); }
    synchronized boolean setPaused(boolean paused) { return handle != 0 && nativeSetPaused(handle, paused); }
    synchronized boolean returnLobby() { return handle != 0 && nativeReturnLobby(handle); }

    synchronized boolean submitInput(int buttons) {
        return handle != 0 && nativeSubmitInput(handle, buttons);
    }

    synchronized long completedFrames() {
        return handle == 0 ? 0 : nativeCompletedFrames(handle);
    }

    synchronized long copyLatestFrame(byte[] rgb565) {
        return handle == 0 || rgb565 == null ? -1 : nativeCopyLatestFrame(handle, rgb565);
    }

    synchronized int pullPcm(short[] samples) {
        return handle == 0 || samples == null ? 0 : nativePullPcm(handle, samples);
    }

    @Override public synchronized void close() {
        if (handle != 0) {
            nativeDestroy(handle);
            handle = 0;
        }
    }

    private static native long nativeCreate();
    private static native boolean nativeHost(long handle, String ipv4, byte[] token);
    private static native String nativeInvite(long handle);
    private static native int[] nativeSnapshot(long handle);
    private static native byte[] nativeSessionId(long handle);
    private static native boolean nativeSelectRom(long handle, byte[] rom);
    private static native boolean nativeSelectGame(long handle, byte[] rom, String key);
    private static native boolean nativeConfirm(long handle);
    private static native boolean nativeSetPaused(long handle, boolean paused);
    private static native boolean nativeReturnLobby(long handle);
    private static native boolean nativeSubmitInput(long handle, int buttons);
    private static native long nativeCompletedFrames(long handle);
    private static native long nativeCopyLatestFrame(long handle, byte[] rgb565);
    private static native int nativePullPcm(long handle, short[] samples);
    private static native void nativeDestroy(long handle);
}
