package com.flynes.emu.video;

import android.content.res.AssetManager;
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
    }

    private static final class JniBridge implements Bridge {
        private final AssetManager assets;
        JniBridge(AssetManager assets) { this.assets = assets; }
        @Override public long create() { return nativeCreate(assets); }
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
            return values == null || values.length < 11 ? NativePresenterStats.EMPTY
                    : new NativePresenterStats(values[0], values[1], values[2], values[3],
                            values[4], values[5], (int) values[6], (int) values[7], values[8],
                            (int) values[9], (int) values[10]);
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
        this(publisher, new JniBridge(assets));
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

    public synchronized long activeEpoch() { return activeEpoch; }

    @Override public synchronized void close() {
        if (handle == 0L) return;
        surfaceReady = false;
        // Native intentionally retains its self-contained worker only when bounded
        // shutdown cannot prove exit; deleting it would risk a use-after-free.
        bridge.destroy(handle);
        handle = 0L;
    }

    private static native long nativeCreate(AssetManager assets);
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
}
