package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;

import android.view.Surface;

import org.junit.Test;

public final class DisplayModeControllerTest {
    @Test
    public void gamesUseDefaultFrameRateCompatibilityRatherThanVideoFixedSource() {
        assertEquals(Surface.FRAME_RATE_COMPATIBILITY_DEFAULT,
                DisplayModeController.frameRateCompatibility());
    }

    @Test
    public void reportsARealModeFallbackAfterTheDisplaySettles() {
        assertEquals("SYSTEM_OR_DEVICE_FALLBACK",
                DisplayModeController.settledFallbackReason(120f, 60f, ""));
        assertEquals("", DisplayModeController.settledFallbackReason(120f, 119.9f, ""));
    }
}
