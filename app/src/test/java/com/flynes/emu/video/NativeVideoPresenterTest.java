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
        int enqueues;
        int destroys;
        long lastSequence;
        @Override public long create() { return 9L; }
        @Override public void destroy(long handle) { }
        @Override public boolean surfaceCreated(long handle, Surface surface, long epoch) {
            return surfaceResult;
        }
        @Override public void surfaceChanged(long handle, int width, int height, long epoch) { }
        @Override public void surfaceDestroyed(long handle, long epoch) { destroys++; }
        @Override public boolean enqueue(long handle, ByteBuffer pixels, long sequence, int width,
                                         int height, int pitch, int format, int bytes) {
            enqueues++;
            lastSequence = sequence;
            return true;
        }
        @Override public void setFilter(long handle, int filter) { }
        @Override public void setActive(long handle, boolean active) { }
        @Override public void resetSequence(long handle) { }
        @Override public NativePresenterStats stats(long handle) { return NativePresenterStats.EMPTY; }
    }
}
