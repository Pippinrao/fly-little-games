package com.flynes.emu.input;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public final class JoystickDirectionTest {
    @Test public void deadZoneSectorAndReleaseHysteresisMatchX100Preset() {
        GamepadHitMap map = GamepadHitMap.fromLayout(
                2340, 1080, 2.75f, 0, 132, 0, 0,
                ControlLayoutV2.recommended(), DirectionControlMode.JOYSTICK, .22f);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float radius = base.width() / 2f;

        assertEquals(0, map.directionBits(base.centerX(), base.centerY(), 0));
        assertEquals(InputBits.RIGHT, map.directionBits(
                base.centerX() + radius * .7f, base.centerY(), 0));
        assertEquals(InputBits.RIGHT | InputBits.DOWN, map.directionBits(
                base.centerX() + radius * .7f, base.centerY() + radius * .7f, 0));
        assertEquals(InputBits.RIGHT, map.directionBits(
                base.centerX() + radius * .18f, base.centerY(), InputBits.RIGHT));
        assertEquals(0, map.directionBits(
                base.centerX() + radius * .14f, base.centerY(), InputBits.RIGHT));
    }

    @Test public void distanceOutsideVisualBaseSaturatesInsteadOfClearing() {
        GamepadHitMap map = GamepadHitMap.fromLayout(
                2340, 1080, 2.75f, 0, 132, 0, 0,
                ControlLayoutV2.recommended(), DirectionControlMode.JOYSTICK, .22f);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float radius = base.width() / 2f;

        assertEquals(InputBits.RIGHT, map.directionBits(
                base.centerX() + radius * 5f, base.centerY(), InputBits.RIGHT));
        assertEquals(GamepadHitMap.Control.NONE, map.hit(
                base.centerX() + radius * 5f, base.centerY()));
    }
}
