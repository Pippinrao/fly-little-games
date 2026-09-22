package com.flynes.emu.ui;

final class NearbyMvpPeerTestBridge implements AutoCloseable {
    static final int LOBBY = 3;
    static final int HOST_P1 = 1;
    static final int GUEST_P2 = 2;

    static { System.loadLibrary("nearby_crypto_test"); }

    private long handle = nativeCreate();

    boolean join(String localIpv4, String invite) {
        return handle != 0 && nativeJoin(handle, localIpv4, invite);
    }

    boolean host(String localIpv4) {
        return handle != 0 && nativeHost(handle, localIpv4);
    }

    String invite() {
        return handle == 0 ? null : nativeInvite(handle);
    }

    int[] snapshot() {
        return handle == 0 ? null : nativeSnapshot(handle);
    }

    byte[] sessionId() {
        return handle == 0 ? null : nativeSessionId(handle);
    }

    @Override public void close() {
        if (handle != 0) {
            nativeDestroy(handle);
            handle = 0;
        }
    }

    private static native long nativeCreate();
    private static native boolean nativeJoin(long handle, String localIpv4, String invite);
    private static native boolean nativeHost(long handle, String localIpv4);
    private static native String nativeInvite(long handle);
    private static native int[] nativeSnapshot(long handle);
    private static native byte[] nativeSessionId(long handle);
    private static native void nativeDestroy(long handle);
}
