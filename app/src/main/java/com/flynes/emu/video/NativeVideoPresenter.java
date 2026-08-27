package com.flynes.emu.video;

import android.content.res.AssetManager;
import android.content.Context;
import android.view.Surface;

import com.flynes.emu.settings.FilterMode;

import java.nio.ByteBuffer;

/** Java lifecycle boundary for the single native EGL presenter. */
public final class NativeVideoPresenter implements AutoCloseable, FrameRateRequestTarget {
    interface Bridge {
        long create();
        boolean destroy(long handle);
        boolean surfaceCreated(long handle, Surface surface, long epoch);
        void surfaceChanged(long handle, int width, int height, long epoch);
        boolean surfaceDestroyed(long handle, long epoch);
        boolean enqueue(long handle, ByteBuffer pixels, long sequence, int width, int height,
                        int pitch, int format, int bytes);
        void setFilter(long handle, int filter);
        void setActive(long handle, boolean active);
        void resetSequence(long handle);
        boolean requestFrameRate(long handle, long epoch, float sourceFps);
        boolean clearFrameRate(long handle, long epoch);
        NativePresenterStats stats(long handle);
        default long actualRealPresentationNs(long handle, long sequence) { return -1L; }
        default boolean configureMotion(long handle, long epoch, long displayGeneration,
                                        float displayHz, float sourceFps, long leaseDeadlineNs,
                                        boolean forcePacerDisabled) { return false; }
        default boolean updateMotionLease(long handle, long epoch, long displayGeneration,
                                          long leaseDeadlineNs) { return false; }
        default long exitMotion(long handle, long epoch, boolean drainToImmediate) {
            return -1L;
        }
        default long beginMotionShadow(long handle, long epoch, float sourceFps) { return -1L; }
    }

    private static final class JniBridge implements Bridge {
        private final AssetManager assets;
        private final Context activityContext;
        JniBridge(AssetManager assets, Context activityContext) {
            this.assets = assets;
            this.activityContext = activityContext;
        }
        @Override public long create() { return nativeCreate(assets, activityContext); }
        @Override public boolean destroy(long handle) { return nativeDestroy(handle); }
        @Override public boolean surfaceCreated(long handle, Surface surface, long epoch) {
            return nativeSurfaceCreated(handle, surface, epoch);
        }
        @Override public void surfaceChanged(long handle, int width, int height, long epoch) {
            nativeSurfaceChanged(handle, width, height, epoch);
        }
        @Override public boolean surfaceDestroyed(long handle, long epoch) {
            return nativeSurfaceDestroyed(handle, epoch);
        }
        @Override public boolean enqueue(long handle, ByteBuffer pixels, long sequence,
                                         int width, int height, int pitch, int format, int bytes) {
            return nativeEnqueue(handle, pixels, sequence, width, height, pitch, format, bytes);
        }
        @Override public void setFilter(long handle, int filter) {
            nativeSetFilter(handle, filter);
        }
        @Override public void setActive(long handle, boolean active) {
            nativeSetActive(handle, active);
        }
        @Override public void resetSequence(long handle) { nativeResetSequence(handle); }
        @Override public boolean requestFrameRate(long handle, long epoch, float sourceFps) {
            return nativeRequestFrameRate(handle, epoch, sourceFps);
        }
        @Override public boolean clearFrameRate(long handle, long epoch) {
            return nativeClearFrameRate(handle, epoch);
        }
        @Override public NativePresenterStats stats(long handle) {
            long[] values = nativeGetStats(handle);
            return values == null || values.length < 36 ? NativePresenterStats.EMPTY
                    : new NativePresenterStats(values[0], values[1], values[2], values[3],
                            values[4], values[5], (int) values[6], (int) values[7], values[8],
                            (int) values[9], (int) values[10], values[11], values[12], values[13],
                            values[14], values[15], values[16], values[17], values[18], values[19],
                            values[20], values[21], values[22], values[23], values[24], values[25],
                            (int) values[26], values[27], (int) values[28],
                            values[29], values[30], values[31], (int) values[32], values[33],
                            values[34], values[35]);
        }
        @Override public long actualRealPresentationNs(long handle, long sequence) {
            return nativeGetActualRealPresentationNs(handle, sequence);
        }
        @Override public boolean configureMotion(long handle, long epoch, long displayGeneration,
                                                 float displayHz, float sourceFps,
                                                 long leaseDeadlineNs,
                                                 boolean forcePacerDisabled) {
            return nativeConfigureMotion(handle, epoch, displayGeneration, displayHz, sourceFps,
                    leaseDeadlineNs, forcePacerDisabled);
        }
        @Override public long beginMotionShadow(long handle, long epoch, float sourceFps) {
            return nativeBeginMotionShadow(handle, epoch, sourceFps);
        }
        @Override public boolean updateMotionLease(long handle, long epoch,
                                                   long displayGeneration,
                                                   long leaseDeadlineNs) {
            return nativeUpdateMotionLease(handle, epoch, displayGeneration, leaseDeadlineNs);
        }
        @Override public long exitMotion(long handle, long epoch, boolean drainToImmediate) {
            return nativeExitMotion(handle, epoch, drainToImmediate);
        }
    }

    private final FramePublisher publisher;
    private final Bridge bridge;
    private long handle;
    private long activeEpoch;
    private boolean surfaceReady;

    public NativeVideoPresenter(FramePublisher publisher) {
        this(publisher, (AssetManager) null);
    }

    public NativeVideoPresenter(FramePublisher publisher, AssetManager assets) {
        this(publisher, new JniBridge(assets, null));
    }

    public NativeVideoPresenter(FramePublisher publisher, Context activityContext) {
        this(publisher, new JniBridge(activityContext == null ? null : activityContext.getAssets(),
                activityContext));
    }

    NativeVideoPresenter(FramePublisher publisher, Bridge bridge) {
        if (publisher == null || bridge == null) {
            throw new IllegalArgumentException("publisher and bridge are required");
        }
        this.publisher = publisher;
        this.bridge = bridge;
        handle = bridge.create();
        if (handle == 0L) throw new IllegalStateException("native presenter creation failed");
    }

    public synchronized boolean surfaceCreated(Surface surface, long epoch) {
        if (handle == 0L || epoch <= activeEpoch) return false;
        surfaceReady = bridge.surfaceCreated(handle, surface, epoch);
        if (surfaceReady) activeEpoch = epoch;
        return surfaceReady;
    }

    public synchronized void surfaceChanged(int width, int height, long epoch) {
        if (handle != 0L && surfaceReady && epoch == activeEpoch && width > 0 && height > 0) {
            bridge.surfaceChanged(handle, width, height, epoch);
        }
    }

    public synchronized boolean surfaceDestroyed(long epoch) {
        if (handle == 0L || !surfaceReady || epoch != activeEpoch) return false;
        boolean confirmed = bridge.surfaceDestroyed(handle, epoch);
        if (confirmed) surfaceReady = false;
        return confirmed;
    }

    public synchronized void onFrameAvailable(long sequence) {
        if (handle == 0L || !surfaceReady || sequence < 0L) return;
        PublishedFrame frame = publisher.poll().orElse(null);
        if (frame == null) return;
        try (frame) {
            ByteBuffer pixels = frame.pixels();
            bridge.enqueue(handle, pixels, frame.sequence(), frame.width(), frame.height(),
                    frame.pitch(), frame.format().ordinal(), pixels.remaining());
        }
    }

    public synchronized void setFilterMode(FilterMode mode) {
        if (handle != 0L) bridge.setFilter(handle,
                (mode == null ? FilterMode.EDGE_ENHANCED : mode).ordinal());
    }

    public synchronized void setActive(boolean active) {
        if (handle != 0L) bridge.setActive(handle, active);
    }

    public synchronized void resetSequence() {
        if (handle != 0L) bridge.resetSequence(handle);
    }

    @Override public synchronized boolean requestFrameRate(long epoch, float sourceFps) {
        return handle != 0L && surfaceReady && epoch == activeEpoch
                && Float.isFinite(sourceFps) && sourceFps > 0f
                && bridge.requestFrameRate(handle, epoch, sourceFps);
    }

    @Override public synchronized boolean clearFrameRate(long epoch) {
        return handle != 0L && surfaceReady && epoch == activeEpoch
                && bridge.clearFrameRate(handle, epoch);
    }

    public synchronized NativePresenterStats stats() {
        return handle == 0L ? NativePresenterStats.EMPTY : bridge.stats(handle);
    }

    public synchronized long actualRealPresentationNs(long sequence) {
        return handle == 0L || sequence < 0L ? -1L
                : bridge.actualRealPresentationNs(handle, sequence);
    }

    /** Certification-only until a complete device profile unlocks Motion in the public resolver. */
    public synchronized boolean configureMotionForTesting(long epoch, long displayGeneration,
                                                          float displayHz, float sourceFps,
                                                          long leaseDeadlineNs,
                                                          boolean forcePacerDisabled) {
        return handle != 0L && surfaceReady && epoch == activeEpoch
                && bridge.configureMotion(handle, epoch, displayGeneration, displayHz, sourceFps,
                        leaseDeadlineNs, forcePacerDisabled);
    }

    public synchronized long beginMotionShadowForTesting(long epoch, float sourceFps) {
        return handle != 0L && surfaceReady && epoch == activeEpoch
                && Float.isFinite(sourceFps) && sourceFps > 0f
                ? bridge.beginMotionShadow(handle, epoch, sourceFps) : -1L;
    }

    public synchronized boolean updateMotionLeaseForTesting(long epoch, long displayGeneration,
                                                            long leaseDeadlineNs) {
        return handle != 0L && surfaceReady && epoch == activeEpoch
                && bridge.updateMotionLease(handle, epoch, displayGeneration, leaseDeadlineNs);
    }

    public synchronized long exitMotion(long epoch, boolean drainToImmediate) {
        return handle != 0L && surfaceReady && epoch == activeEpoch
                ? bridge.exitMotion(handle, epoch, drainToImmediate) : -1L;
    }

    public synchronized long activeEpoch() { return activeEpoch; }

    @Override public synchronized void close() {
        if (handle == 0L) return;
        surfaceReady = false;
        // Native intentionally retains its self-contained worker only when bounded
        // shutdown cannot prove exit; deleting it would risk a use-after-free.
        bridge.destroy(handle);
        handle = 0L;
    }

    private static native long nativeCreate(AssetManager assets, Context activityContext);
    private static native boolean nativeDestroy(long handle);
    private static native boolean nativeSurfaceCreated(long handle, Surface surface, long epoch);
    private static native void nativeSurfaceChanged(long handle, int width, int height, long epoch);
    private static native boolean nativeSurfaceDestroyed(long handle, long epoch);
    private static native boolean nativeEnqueue(long handle, ByteBuffer pixels, long sequence,
                                                int width, int height, int pitch, int format,
                                                int bytes);
    private static native void nativeSetFilter(long handle, int filter);
    private static native void nativeSetActive(long handle, boolean active);
    private static native void nativeResetSequence(long handle);
    private static native boolean nativeRequestFrameRate(long handle, long epoch,
                                                         float sourceFps);
    private static native boolean nativeClearFrameRate(long handle, long epoch);
    private static native long[] nativeGetStats(long handle);
    private static native long nativeGetActualRealPresentationNs(long handle, long sequence);
    private static native boolean nativeConfigureMotion(
            long handle, long epoch, long displayGeneration, float displayHz, float sourceFps,
            long leaseDeadlineNs, boolean forcePacerDisabled);
    private static native long nativeBeginMotionShadow(long handle, long epoch, float sourceFps);
    private static native boolean nativeUpdateMotionLease(
            long handle, long epoch, long displayGeneration, long leaseDeadlineNs);
    private static native long nativeExitMotion(
            long handle, long epoch, boolean drainToImmediate);
}
