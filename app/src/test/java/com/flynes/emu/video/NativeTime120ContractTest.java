package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.view.Surface;

import org.junit.Test;

import java.nio.ByteBuffer;

public final class NativeTime120ContractTest {
    @Test public void oneHundredTwentyDisplayTicksDoNotDuplicateSixtySourceFrames() {
        RepeatedSequenceSource source = new RepeatedSequenceSource();
        CountingBridge bridge = new CountingBridge();
        NativeVideoPresenter presenter = new NativeVideoPresenter(
                new FramePublisher(source), bridge);
        assertTrue(presenter.surfaceCreated(null, 1L));

        for (int tick = 0; tick < 120; tick++) presenter.onFrameAvailable(tick / 2L);

        assertEquals(60, bridge.enqueues);
        assertEquals(59L, bridge.lastSequence);
        presenter.close();
    }

    private static final class RepeatedSequenceSource implements FramePublisher.Source {
        int copies;
        @Override public PublishedFrame copyLatest() {
            long sequence = copies++ / 2L;
            return new PublishedFrame(sequence, 4, 2, 8,
                    PublishedFrame.Format.RGB565, ByteBuffer.allocateDirect(16), true);
        }
    }

    private static final class CountingBridge implements NativeVideoPresenter.Bridge {
        int enqueues;
        long lastSequence;
        @Override public long create() { return 1L; }
        @Override public boolean destroy(long handle) { return true; }
        @Override public boolean surfaceCreated(long handle, Surface surface, long epoch) {
            return true;
        }
        @Override public void surfaceChanged(long handle, int width, int height, long epoch) { }
        @Override public boolean surfaceDestroyed(long handle, long epoch) { return true; }
        @Override public boolean enqueue(long handle, ByteBuffer pixels, long sequence,
                                         int width, int height, int pitch, int format, int bytes) {
            enqueues++;
            lastSequence = sequence;
            return true;
        }
        @Override public void setFilter(long handle, int filter) { }
        @Override public void setActive(long handle, boolean active) { }
        @Override public void resetSequence(long handle) { }
        @Override public boolean requestFrameRate(long handle, long epoch, float sourceFps) {
            return true;
        }
        @Override public boolean clearFrameRate(long handle, long epoch) { return true; }
        @Override public NativePresenterStats stats(long handle) { return NativePresenterStats.EMPTY; }
    }
}
