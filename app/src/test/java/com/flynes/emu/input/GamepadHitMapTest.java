package com.flynes.emu.input;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public final class GamepadHitMapTest {
    @Test
    public void standardLayoutHasNoOverlaps() {
        GamepadHitMap map = GamepadHitMap.standard(2340, 1080, 2.75f, 0, 132, 0, 0);
        assertTrue(map.validate().toString(), map.validate().isEmpty());
    }

    @Test
    public void aAndBHaveAnUnambiguousGap() {
        GamepadHitMap map = GamepadHitMap.standard(2340, 1080, 2.75f, 0, 132, 0, 0);
        GamepadHitMap.Circle b = map.circle(GamepadHitMap.Control.B);
        GamepadHitMap.Circle a = map.circle(GamepadHitMap.Control.A);

        assertEquals(GamepadHitMap.Control.B, map.hit(b.cx(), b.cy()));
        assertEquals(GamepadHitMap.Control.A, map.hit(a.cx(), a.cy()));
        assertEquals(GamepadHitMap.Control.NONE,
                map.hit((b.cx() + a.cx()) / 2f, b.cy()));
    }

    @Test
    public void rightInsetKeepsAInsideGestureSafeArea() {
        int width = 2400;
        int insetRight = 160;
        GamepadHitMap map = GamepadHitMap.standard(width, 1080, 3f, 0, insetRight, 0, 0);
        GamepadHitMap.Circle a = map.circle(GamepadHitMap.Control.A);

        assertTrue(a.cx() + a.radius() <= width - insetRight);
    }
}
