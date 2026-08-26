package com.flynes.emu.video;

import android.content.Context;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import com.flynes.emu.settings.FilterMode;

/** Surface-only view. NativeVideoPresenter is the sole EGL and buffer-swap owner. */
public final class GameSurfaceView extends SurfaceView implements SurfaceHolder.Callback {
    public interface Listener {
        void onSurfaceAvailable(Surface surface, long epoch);
        void onSurfaceLost(long epoch);
    }

    private final NativeVideoPresenter presenter;
    private final Listener listener;
    private long epoch;

    public GameSurfaceView(Context context, FramePublisher publisher, Listener listener) {
        super(context);
        this.presenter = new NativeVideoPresenter(publisher);
        this.listener = listener;
        getHolder().addCallback(this);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    @Override public void surfaceCreated(SurfaceHolder holder) {
        long nextEpoch = Math.addExact(epoch, 1L);
        if (!presenter.surfaceCreated(holder.getSurface(), nextEpoch)) return;
        epoch = nextEpoch;
        if (listener != null) listener.onSurfaceAvailable(holder.getSurface(), epoch);
    }

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        presenter.surfaceChanged(width, height, epoch);
    }

    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        presenter.surfaceDestroyed(epoch);
        if (listener != null) listener.onSurfaceLost(epoch);
    }

    public void onFrameAvailable(long sequence) { presenter.onFrameAvailable(sequence); }
    public void setFilterMode(FilterMode mode) { presenter.setFilterMode(mode); }
    public void onResume() { presenter.setActive(true); }
    public void onPause() { presenter.setActive(false); }
    public void resetSequence() { presenter.resetSequence(); }
    public NativePresenterStats presenterStats() { return presenter.stats(); }
    public long surfaceEpoch() { return epoch; }
    public void release() { presenter.close(); }
}
