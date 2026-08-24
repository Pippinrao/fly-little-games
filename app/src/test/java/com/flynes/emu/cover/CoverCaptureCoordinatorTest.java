package com.flynes.emu.cover;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.video.PublishedFrame;

import org.junit.Test;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.List;

public final class CoverCaptureCoordinatorTest {
    @Test public void skipsBlankCandidateAndStoresLaterInformativeFrame() {
        List<CoverFrame> stored = new ArrayList<>();
        CoverCaptureCoordinator coordinator = new CoverCaptureCoordinator(
                "game-one", stored::add, Runnable::run);

        coordinator.onFrame(frame(1, false));
        coordinator.onFrame(frame(121, false));
        coordinator.onFrame(frame(241, true));

        assertEquals(1, stored.size());
        assertEquals("game-one", stored.get(0).canonicalId());
        assertTrue(FrameQuality.score(stored.get(0)) >= FrameQuality.MIN_ACCEPTABLE);
    }

    @Test public void capturedBytesDoNotChangeWhenNativeBufferIsReused() {
        ByteBuffer pixels = ByteBuffer.allocateDirect(8);
        for (int i = 0; i < 8; i++) pixels.put(i, (byte) (i + 1));
        PublishedFrame published = new PublishedFrame(7, 2, 2, 4,
                PublishedFrame.Format.RGB565, pixels, true);

        CoverFrame copy = CoverFrame.copyOf("game", published);
        pixels.put(0, (byte) 99);

        assertEquals(1, copy.pixels()[0]);
    }

    private static PublishedFrame frame(long sequence, boolean checker) {
        int width = 32, height = 32;
        ByteBuffer pixels = ByteBuffer.allocateDirect(width * height * 2);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int rgb565 = checker && ((x / 4 + y / 4) & 1) == 0 ? 0xffff : 0x001f;
                pixels.put((byte) (rgb565 & 0xff));
                pixels.put((byte) ((rgb565 >>> 8) & 0xff));
            }
        }
        pixels.position(0);
        return new PublishedFrame(sequence, width, height, width * 2,
                PublishedFrame.Format.RGB565, pixels, true);
    }
}
