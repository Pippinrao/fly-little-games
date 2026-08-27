package com.flynes.emu.video;

import android.content.Context;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

import com.flynes.emu.settings.FilterMode;

/** Surface-only view. NativeVideoPresenter is the sole EGL and buffer-swap owner. */
public final class GameSurfaceView extends SurfaceView implements SurfaceHolder.Callback,
        FrameRateRequestTarget {
    public interface Listener {
        void onSurfaceAvailable(Surface surface, long epoch);
        default void onSurfaceDestroying(long epoch) { }
        void onSurfaceLost(long epoch);
    }

    private final NativeVideoPresenter presenter;
    private final Listener listener;
    private long epoch;
    private boolean hasFrameRateVote;
    private boolean motionOwnsPacing;

    public GameSurfaceView(Context context, FramePublisher publisher, Listener listener) {
        super(context);
        this.presenter = new NativeVideoPresenter(publisher, context);
        this.listener = listener;
        getHolder().addCallback(this);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    @Override public void surfaceCreated(SurfaceHolder holder) {
        // Do not discard an unconfirmed old-epoch vote. The new epoch's first
        // clear/request must settle the native pending token before changing mode.
        long nextEpoch = Math.addExact(epoch, 1L);
        if (!presenter.surfaceCreated(holder.getSurface(), nextEpoch)) return;
        epoch = nextEpoch;
        motionOwnsPacing = false;
        if (listener != null) listener.onSurfaceAvailable(holder.getSurface(), epoch);
    }

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        presenter.surfaceChanged(width, height, epoch);
    }

    @Override public void surfaceDestroyed(SurfaceHolder holder) {
        if (listener != null) listener.onSurfaceDestroying(epoch);
        boolean confirmed = presenter.surfaceDestroyed(epoch);
        // Keep tracking a potentially live vote when the bounded destroy times out;
        // the listener's clear path must not release the Java window preference early.
        if (confirmed) hasFrameRateVote = false;
        if (confirmed) motionOwnsPacing = false;
        if (listener != null) listener.onSurfaceLost(epoch);
    }

    public void onFrameAvailable(long sequence) { presenter.onFrameAvailable(sequence); }
    public void setFilterMode(FilterMode mode) { presenter.setFilterMode(mode); }
    public void onResume() { presenter.setActive(true); }
    public void onPause() { presenter.setActive(false); }
    public void resetSequence() { presenter.resetSequence(); }
    @Override public boolean requestFrameRate(long surfaceEpoch, float sourceFps) {
        if (motionOwnsPacing) return false;
        // Track the attempt as a potential vote: a timeout can race a late platform
        // apply, so the caller must be able to issue a compensating clear.
        hasFrameRateVote = true;
        boolean accepted = presenter.requestFrameRate(surfaceEpoch, sourceFps);
        return accepted;
    }
    @Override public boolean clearFrameRate(long surfaceEpoch) {
        if (motionOwnsPacing) return false;
        if (!hasFrameRateVote) return true;
        boolean cleared = presenter.clearFrameRate(surfaceEpoch);
        if (cleared) hasFrameRateVote = false;
        return cleared;
    }
    public NativePresenterStats presenterStats() { return presenter.stats(); }
    public long actualRealPresentationNs(long sequence) {
        return presenter.actualRealPresentationNs(sequence);
    }
    public long surfaceEpoch() { return epoch; }
    public boolean configureMotionForTesting(long displayGeneration, float displayHz,
                                             float sourceFps, long leaseDeadlineNs,
                                             boolean forcePacerDisabled) {
        boolean configured = presenter.configureMotionForTesting(epoch, displayGeneration,
                displayHz, sourceFps, leaseDeadlineNs, forcePacerDisabled);
        if (configured) {
            hasFrameRateVote = false;
            motionOwnsPacing = true;
        }
        return configured;
    }
    public long beginMotionShadow(float sourceFps) {
        return presenter.beginMotionShadowForTesting(epoch, sourceFps);
    }
    public boolean updateMotionLease(long displayGeneration, long leaseDeadlineNs) {
        return presenter.updateMotionLeaseForTesting(epoch, displayGeneration, leaseDeadlineNs);
    }
    public long exitMotion(boolean drainToImmediate) {
        long transitionId = presenter.exitMotion(epoch, drainToImmediate);
        if (transitionId >= 0L) {
            motionOwnsPacing = false;
            hasFrameRateVote = true;
        }
        return transitionId;
    }
    public void release() {
        hasFrameRateVote = false;
        motionOwnsPacing = false;
        presenter.close();
    }
}
