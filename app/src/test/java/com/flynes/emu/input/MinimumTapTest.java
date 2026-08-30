package com.flynes.emu.input;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public final class MinimumTapTest {
    @Test
    public void quickTapKeepsButtonPressedForTheRemainingMinimumDuration() {
        assertEquals(16L, MinimumTap.remainingMillis(100L, 101L, 17L));
    }

    @Test
    public void normalHumanPressNeedsNoExtraPulse() {
        assertEquals(0L, MinimumTap.remainingMillis(100L, 175L, 17L));
    }

    @Test
    public void clockAnomalyCannotExtendBeyondTheConfiguredMinimum() {
        assertEquals(17L, MinimumTap.remainingMillis(100L, 90L, 17L));
    }
}
