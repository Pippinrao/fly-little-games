package com.flynes.emu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.video.MotionOffscreenRenderer;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class MotionContextFallbackTest {
    @Test public void es31CreationFailureIsExplicitAndNeverReturnsSyntheticPixels() {
        short[] frame = new short[256 * 240];
        MotionOffscreenRenderer.Result result = MotionOffscreenRenderer.renderGpu(
                frame, frame, 256, 240, true);
        assertFalse(result.succeeded());
        assertEquals("EGL_CONTEXT_UNAVAILABLE", result.failureDetail());
        assertEquals(0, result.pixels().length);
    }
}
