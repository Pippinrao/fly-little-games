package com.flynes.emu;

import java.util.function.LongConsumer;

/** Admission only: no monitor is held across JNI, provider callbacks, or ROM IO. */
final class NearbyOwnerHandle {
    private final long handle;
    private boolean closing;
    private int users;

    NearbyOwnerHandle(long handle) { this.handle = handle; }
    synchronized Lease acquire() {
        if (closing) return new Lease(null, 0);
        Lease lease = new Lease(this, handle);
        ++users;
        return lease;
    }
    void close(Runnable cancelPreparation, LongConsumer destroy) {
        synchronized (this) {
            if (closing) return;
            closing = true;
        }
        boolean interrupted = false;
        try {
            cancelPreparation.run();
        } finally {
            synchronized (this) {
                while (users != 0) {
                    try { wait(); }
                    catch (InterruptedException ignored) { interrupted = true; }
                }
            }
            try { if (handle != 0) destroy.accept(handle); }
            finally { if (interrupted) Thread.currentThread().interrupt(); }
        }
    }
    static final class Lease implements AutoCloseable {
        private NearbyOwnerHandle owner;
        private final long handle;
        private Lease(NearbyOwnerHandle owner, long handle) { this.owner = owner; this.handle = handle; }
        long value() { return handle; }
        @Override public void close() {
            NearbyOwnerHandle admitted = owner;
            if (admitted == null) return;
            synchronized (admitted) {
                if (owner == null) return;
                owner = null;
                --admitted.users;
                admitted.notifyAll();
            }
        }
    }
}
