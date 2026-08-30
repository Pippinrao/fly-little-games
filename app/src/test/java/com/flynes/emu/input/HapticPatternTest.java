package com.flynes.emu.input;

import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertEquals;

import java.util.HashSet;
import java.util.Set;

import org.junit.Test;

public final class HapticPatternTest {
    @Test
    public void x100ProfileGivesEveryOnScreenControlADistinctIdentity() {
        Set<HapticPattern> patterns = new HashSet<>();
        for (GamepadHitMap.Control control : new GamepadHitMap.Control[]{
                GamepadHitMap.Control.UP, GamepadHitMap.Control.DOWN,
                GamepadHitMap.Control.LEFT, GamepadHitMap.Control.RIGHT,
                GamepadHitMap.Control.A, GamepadHitMap.Control.B,
                GamepadHitMap.Control.SELECT, GamepadHitMap.Control.START,
                GamepadHitMap.Control.PAUSE}) {
            patterns.add(HapticPattern.forControl(control, HapticLevel.LIGHT, true));
        }
        assertEquals(9, patterns.size());
    }
    @Test
    public void distinctABUsesDifferentCadenceAndCanBeDisabled() {
        assertNotEquals(
                HapticPattern.forControl(GamepadHitMap.Control.A, HapticLevel.LIGHT, true),
                HapticPattern.forControl(GamepadHitMap.Control.B, HapticLevel.LIGHT, true));
        assertEquals(
                HapticPattern.forControl(GamepadHitMap.Control.A, HapticLevel.LIGHT, false),
                HapticPattern.forControl(GamepadHitMap.Control.B, HapticLevel.LIGHT, false));
        assertEquals(HapticPattern.NONE,
                HapticPattern.forControl(GamepadHitMap.Control.A, HapticLevel.OFF, true));
    }

    @Test
    public void strongerLevelsIncreaseAmplitudeWithoutChangingButtonIdentity() {
        HapticPattern light = HapticPattern.forControl(
                GamepadHitMap.Control.A, HapticLevel.LIGHT, true);
        HapticPattern strong = HapticPattern.forControl(
                GamepadHitMap.Control.A, HapticLevel.STRONG, true);

        assertEquals(light.timings().length, strong.timings().length);
        assertNotEquals(light.amplitudes()[1], strong.amplitudes()[1]);
    }
}
