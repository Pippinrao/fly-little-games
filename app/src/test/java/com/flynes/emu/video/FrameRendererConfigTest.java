package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;

import android.opengl.GLES20;

import com.flynes.emu.settings.FilterMode;

import org.junit.Test;

public final class FrameRendererConfigTest {
    @Test
    public void nearestUsesNearestSamplingAndOtherLegacyModesUseLinearSampling() {
        assertEquals(GLES20.GL_NEAREST, FrameRenderer.textureFilter(FilterMode.NEAREST));
        assertEquals(GLES20.GL_LINEAR, FrameRenderer.textureFilter(FilterMode.SMOOTH));
        assertEquals(GLES20.GL_LINEAR, FrameRenderer.textureFilter(FilterMode.HQ4X));
    }
}
