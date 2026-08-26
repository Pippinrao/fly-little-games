package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.video.quality.SourceTiming;

import org.junit.Test;

import java.nio.ByteBuffer;

public final class NativeFrameSourceTest {
    @Test public void newNoChangeAndErrorAreNotConfusedWithSequenceValues() {
        FakeBridge bridge = new FakeBridge();
        NativeFrameSource source = new NativeFrameSource(bridge, 128);

        PublishedFrame first = source.copyLatest();
        assertEquals(12L, first.sequence());
        assertEquals(-1L, bridge.requestedSequence);

        bridge.next = FrameCopyResult.noChange();
        assertFalse(source.copyLatest() != null);
        assertEquals(12L, bridge.requestedSequence);

        bridge.next = FrameCopyResult.error(-100);
        assertFalse(source.copyLatest() != null);
        assertEquals(12L, bridge.requestedSequence);
    }

    @Test public void resultFactoriesRejectAmbiguousStatusValues() {
        assertEquals(FrameCopyResult.Kind.NO_CHANGE, FrameCopyResult.noChange().kind());
        assertEquals(-4, FrameCopyResult.error(-4).errorCode());
        assertTrue(FrameCopyResult.newFrame(0L, 1L, SourceTiming.PAL_50).sequence() == 0L);
    }

    private static final class FakeBridge implements NativeFrameSource.Bridge {
        FrameCopyResult next = FrameCopyResult.newFrame(12L, 99L,
                SourceTiming.NTSC_60_0988);
        long requestedSequence;

        @Override public FrameCopyResult copyVideoFrameIfNew(ByteBuffer destination,
                                                              int[] metadata,
                                                              long lastSequence) {
            requestedSequence = lastSequence;
            if (next.kind() == FrameCopyResult.Kind.NEW) {
                metadata[0] = 4;
                metadata[1] = 4;
                metadata[2] = 8;
                metadata[3] = PublishedFrame.Format.RGB565.ordinal();
                metadata[4] = 32;
                destination.clear();
                for (int index = 0; index < 32; index++) destination.put((byte) index);
            }
            return next;
        }
    }
}
