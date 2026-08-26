package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.view.Surface;

import java.nio.ByteBuffer;

import org.junit.Test;

public final class NativeVideoPresenterTest {
    @Test public void copiesEachPublishedSequenceAtMostOnce() {
        FakeSource source = new FakeSource();
        FramePublisher publisher = new FramePublisher(source);
        FakeBridge bridge = new FakeBridge();
        NativeVideoPresenter presenter = new NativeVideoPresenter(publisher, bridge);
        bridge.surfaceResult = true;
        assertTrue(presenter.surfaceCreated(nullSurface(), 1L));

        presenter.onFrameAvailable(4L);
        presenter.onFrameAvailable(4L);

        assertEquals(1, bridge.enqueues);
        assertEquals(4L, bridge.lastSequence);
        presenter.close();
    }

    @Test public void destroyRejectsFramesAndStaleLifecycleCallbacks() {
        FakeBridge bridge = new FakeBridge();
        bridge.surfaceResult = true;
        NativeVideoPresenter presenter = new NativeVideoPresenter(
                new FramePublisher(new FakeSource()), bridge);
        assertTrue(presenter.surfaceCreated(nullSurface(), 2L));
        presenter.surfaceDestroyed(1L);
        presenter.onFrameAvailable(1L);
        assertEquals(1, bridge.enqueues);

        presenter.surfaceDestroyed(2L);
        presenter.onFrameAvailable(2L);
        assertEquals(1, bridge.enqueues);
        assertEquals(1, bridge.destroys);
        assertFalse(presenter.surfaceCreated(nullSurface(), 2L));
        presenter.close();
    }

    @Test public void frameRateVotesAreBoundToTheActiveSurfaceEpoch() {
        FakeBridge bridge = new FakeBridge();
        bridge.surfaceResult = true;
        NativeVideoPresenter presenter = new NativeVideoPresenter(
                new FramePublisher(new FakeSource()), bridge);

        assertFalse(presenter.requestFrameRate(1L, 60.0988f));
        assertTrue(presenter.surfaceCreated(nullSurface(), 2L));
        assertFalse(presenter.requestFrameRate(1L, 60.0988f));
        assertTrue(presenter.requestFrameRate(2L, 60.0988f));
        assertEquals(1, bridge.frameRateRequests);
        assertFalse(presenter.clearFrameRate(1L));
        assertTrue(presenter.clearFrameRate(2L));
        assertEquals(1, bridge.frameRateClears);
        presenter.close();
    }

    @Test public void destroyTimeoutKeepsEpochClearableForCompensation() {
        FakeBridge bridge = new FakeBridge();
        bridge.surfaceResult = true;
        bridge.surfaceDestroyResult = false;
        bridge.clearFailuresRemaining = 1;
        NativeVideoPresenter presenter = new NativeVideoPresenter(
                new FramePublisher(new FakeSource()), bridge);
        assertTrue(presenter.surfaceCreated(nullSurface(), 3L));

        assertFalse(presenter.surfaceDestroyed(3L));
        assertFalse(presenter.clearFrameRate(3L));
        assertTrue(presenter.clearFrameRate(3L));
        assertEquals(2, bridge.frameRateClears);
        presenter.close();
    }

    private static Surface nullSurface() {
        // The fake bridge never dereferences the framework object. A real JNI bridge rejects null.
        return null;
    }

    private static final class FakeSource implements FramePublisher.Source {
        @Override public PublishedFrame copyLatest() {
            ByteBuffer pixels = ByteBuffer.allocateDirect(16);
            return new PublishedFrame(4L, 4, 2, 8, PublishedFrame.Format.RGB565, pixels, true);
        }
    }

    private static final class FakeBridge implements NativeVideoPresenter.Bridge {
        boolean surfaceResult;
        boolean surfaceDestroyResult = true;
        int enqueues;
        int destroys;
        int frameRateRequests;
        int frameRateClears;
        int clearFailuresRemaining;
        long lastSequence;
        @Override public long create() { return 9L; }
        @Override public boolean destroy(long handle) { return true; }
        @Override public boolean surfaceCreated(long handle, Surface surface, long epoch) {
            return surfaceResult;
        }
        @Override public void surfaceChanged(long handle, int width, int height, long epoch) { }
        @Override public boolean surfaceDestroyed(long handle, long epoch) {
            destroys++;
            return surfaceDestroyResult;
        }
        @Override public boolean enqueue(long handle, ByteBuffer pixels, long sequence, int width,
                                         int height, int pitch, int format, int bytes) {
            enqueues++;
            lastSequence = sequence;
            return true;
        }
        @Override public void setFilter(long handle, int filter) { }
        @Override public void setActive(long handle, boolean active) { }
        @Override public void resetSequence(long handle) { }
        @Override public boolean requestFrameRate(long handle, long epoch, float sourceFps) {
            frameRateRequests++;
            return true;
        }
        @Override public boolean clearFrameRate(long handle, long epoch) {
            frameRateClears++;
            if (clearFailuresRemaining > 0) {
                clearFailuresRemaining--;
                return false;
            }
            return true;
        }
        @Override public NativePresenterStats stats(long handle) { return NativePresenterStats.EMPTY; }
    }
}
