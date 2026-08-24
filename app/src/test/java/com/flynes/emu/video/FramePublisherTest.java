package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.nio.ByteBuffer;

import org.junit.Test;

public final class FramePublisherTest {
    @Test
    public void emitsOnlyWhenSequenceChanges() {
        FakeFrameSource source = new FakeFrameSource(7);
        FramePublisher publisher = new FramePublisher(source);

        assertEquals(7, publisher.poll().orElseThrow().sequence());
        assertTrue(publisher.poll().isEmpty());

        source.sequence = 8;
        assertEquals(8, publisher.poll().orElseThrow().sequence());
    }

    @Test
    public void rejectsIncompleteOrRegressingFrames() {
        FakeFrameSource source = new FakeFrameSource(4);
        FramePublisher publisher = new FramePublisher(source);
        assertTrue(publisher.poll().isPresent());

        source.sequence = 3;
        assertFalse(publisher.poll().isPresent());
        source.sequence = 5;
        source.complete = false;
        assertFalse(publisher.poll().isPresent());
    }

    @Test
    public void mapsValidatedNativeMetadataToAReadOnlyFrame() {
        ByteBuffer storage = ByteBuffer.allocateDirect(256 * 240 * 2);
        PublishedFrame frame = PublishedFrame.fromNative(
                12, new int[]{256, 240, 512, 0, storage.capacity()}, storage);

        assertEquals(12, frame.sequence());
        assertEquals(PublishedFrame.Format.RGB565, frame.format());
        assertEquals(storage.capacity(), frame.pixels().remaining());
        assertTrue(frame.pixels().isReadOnly());
    }

    @Test(expected = IllegalArgumentException.class)
    public void rejectsNativeMetadataThatExceedsTheDirectBuffer() {
        ByteBuffer storage = ByteBuffer.allocateDirect(32);
        PublishedFrame.fromNative(1, new int[]{256, 240, 512, 0, 64}, storage);
    }

    @Test
    public void nativeFrameSourceReturnsNullOnBridgeErrorAndMapsSuccess() {
        NativeFrameSource.Bridge bridge = (destination, metadata) -> {
            metadata[0] = 4;
            metadata[1] = 2;
            metadata[2] = 8;
            metadata[3] = 0;
            metadata[4] = 16;
            return 9;
        };
        NativeFrameSource source = new NativeFrameSource(bridge, 64);
        assertEquals(9, source.copyLatest().sequence());

        NativeFrameSource failing = new NativeFrameSource((destination, metadata) -> -3, 64);
        assertEquals(null, failing.copyLatest());
    }

    private static final class FakeFrameSource implements FramePublisher.Source {
        long sequence;
        boolean complete = true;

        FakeFrameSource(long sequence) {
            this.sequence = sequence;
        }

        @Override public PublishedFrame copyLatest() {
            ByteBuffer pixels = ByteBuffer.allocateDirect(256 * 240 * 2).asReadOnlyBuffer();
            return new PublishedFrame(sequence, 256, 240, 256 * 2,
                    PublishedFrame.Format.RGB565, pixels, complete);
        }
    }
}
