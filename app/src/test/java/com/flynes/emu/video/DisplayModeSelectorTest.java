package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public final class DisplayModeSelectorTest {
    private static final DisplayCandidate[] MODES = {
            new DisplayCandidate(1, 2340, 1080, 60f),
            new DisplayCandidate(2, 2340, 1080, 90f),
            new DisplayCandidate(3, 2340, 1080, 120f),
            new DisplayCandidate(4, 1920, 1080, 144f)
    };

    @Test
    public void autoChoosesHighestNativeResolutionRefreshRate() {
        assertEquals(3, DisplayModeSelector.select(
                MODES, 2340, 1080, RefreshMode.AUTO));
    }

    @Test
    public void request120ChoosesMatchingNativeResolution() {
        assertEquals(3, DisplayModeSelector.select(
                MODES, 2340, 1080, RefreshMode.HZ_120));
    }

    @Test
    public void unsupported90FallsBackToSystemAuto() {
        DisplayCandidate[] only60 = {new DisplayCandidate(1, 2340, 1080, 60f)};
        assertEquals(0, DisplayModeSelector.select(
                only60, 2340, 1080, RefreshMode.HZ_90));
    }

    @Test
    public void neverChoosesAHighRefreshModeAtTheWrongResolution() {
        assertEquals(1, DisplayModeSelector.select(
                MODES, 2340, 1080, RefreshMode.HZ_60));
    }
}
