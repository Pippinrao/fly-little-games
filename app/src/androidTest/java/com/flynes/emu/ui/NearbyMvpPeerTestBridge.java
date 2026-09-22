package com.flynes.emu.ui;

final class NearbyMvpPeerTestBridge implements AutoCloseable {
    static final int LOBBY = 3;

    static { System.loadLibrary("nearby_crypto_test"); }

    private long handle = nativeCreate();

    boolean join(String localIpv4, String invite) {
        return handle != 0 && nativeJoin(handle, localIpv4, invite);
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
    private static native int[] nativeSnapshot(long handle);
    private static native byte[] nativeSessionId(long handle);
    private static native void nativeDestroy(long handle);
}
