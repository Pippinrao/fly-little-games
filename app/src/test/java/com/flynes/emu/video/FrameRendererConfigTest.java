package com.flynes.emu.video;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.settings.FilterMode;

import org.junit.Test;

public final class FrameRendererConfigTest {
    @Test
    public void everyFilterUsesARealPurposeBuiltShader() {
        String edge = FrameRenderer.fragmentShader(FilterMode.EDGE_ENHANCED);
        String sharp = FrameRenderer.fragmentShader(FilterMode.SHARP_BILINEAR);
        String nearest = FrameRenderer.fragmentShader(FilterMode.NEAREST);
        String crt = FrameRenderer.fragmentShader(FilterMode.CRT);

        assertTrue(edge.contains("uTextureSize"));
        assertTrue(edge.contains("colorDistance"));
        assertTrue(sharp.contains("sharpFraction"));
        assertFalse(nearest.contains("uOutputSize"));
        assertTrue(crt.contains("uOutputSize"));
        assertTrue(crt.contains("scanline"));
    }
}
