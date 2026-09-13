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

    /**
     * Every bundled homebrew title opens on a black screen carrying a few lines
     * of thin white credit text, which is exactly the frame a cover should show.
     * Point-sampling one pixel out of every 64 skipped the glyph strokes
     * entirely, so the frame scored as an empty black screen and every bundled
     * game was refused a cover.
     */
    @Test public void scoresThinTextOnBlackAboveTheAcceptanceThreshold() {
        CoverFrame credits = textFrame("game-one", 6);

        assertTrue("thin credit text must be capturable, scored "
                        + FrameQuality.score(credits),
                FrameQuality.score(credits) >= FrameQuality.MIN_ACCEPTABLE);
    }

    @Test public void stillRejectsABlankBlackFrame() {
        CoverFrame blank = textFrame("game-one", 0);

        assertTrue("a black boot screen must stay rejected, scored "
                        + FrameQuality.score(blank),
                FrameQuality.score(blank) < FrameQuality.MIN_ACCEPTABLE);
    }

    /**
     * Builds a black frame painted with `lines` rows of 2-pixel-tall, 8-pixel-wide
     * white glyphs with a gap between glyphs, the way a title screen renders its
     * credits inside 8x8 tiles.
     */
    private static CoverFrame textFrame(String canonicalId, int lines) {
        int width = 256, height = 240;
        ByteBuffer pixels = ByteBuffer.allocateDirect(width * height * 2);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                boolean painted = false;
                if (lines > 0 && y >= 24 && y < 24 + lines * 24) {
                    int withinLine = (y - 24) % 24;
                    painted = withinLine < 14 && x >= 16 && x < 224 && (x % 8) < 6;
                }
                int rgb565 = painted ? 0xffff : 0x0000;
                pixels.put((byte) (rgb565 & 0xff));
                pixels.put((byte) ((rgb565 >>> 8) & 0xff));
            }
        }
        pixels.position(0);
        PublishedFrame published = new PublishedFrame(1, width, height, width * 2,
                PublishedFrame.Format.RGB565, pixels, true);
        return CoverFrame.copyOf(canonicalId, published);
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
