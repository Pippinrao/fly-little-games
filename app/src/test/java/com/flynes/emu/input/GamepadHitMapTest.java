package com.flynes.emu.input;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public final class GamepadHitMapTest {
    private static final float DENSITY = 2.75f;

    @Test public void standardLayoutUsesClassicDpadAndErgonomicActionSizes() {
        GamepadHitMap map = GamepadHitMap.standard(2340, 1080, DENSITY, 0, 132, 0, 0);
        assertEquals(144f * DENSITY, map.dpadBounds().width(), .1f);
        assertEquals(144f * DENSITY, map.dpadBounds().height(), .1f);
        assertEquals(72f * DENSITY, map.target(GamepadHitMap.Control.A).width(), .1f);
        assertEquals(64f * DENSITY, map.target(GamepadHitMap.Control.B).width(), .1f);
        assertFalse(map.controls().contains(GamepadHitMap.Control.NONE));
        assertTrue(map.validate().toString(), map.validate().isEmpty());
    }

    @Test public void aAndBAreDiagonalWithAtLeastTwentyFourDpGap() {
        GamepadHitMap map = GamepadHitMap.standard(2340, 1080, DENSITY, 0, 132, 0, 0);
        GamepadHitMap.Target a = map.target(GamepadHitMap.Control.A);
        GamepadHitMap.Target b = map.target(GamepadHitMap.Control.B);
        assertTrue(a.centerX() > b.centerX());
        assertTrue(a.centerY() < b.centerY());
        assertTrue(map.distanceBetween(a, b) >= 24f * DENSITY);
    }

    @Test public void selectAndStartStayOutsideCentralSeventyPercent() {
        int width = 2340;
        GamepadHitMap map = GamepadHitMap.standard(width, 1080, DENSITY, 0, 132, 0, 0);
        assertTrue(map.target(GamepadHitMap.Control.SELECT).right() <= width * .15f);
        assertTrue(map.target(GamepadHitMap.Control.START).left() >= width * .85f);
        assertTrue(map.target(GamepadHitMap.Control.SELECT).width() >= 72f * DENSITY);
        assertTrue(map.target(GamepadHitMap.Control.START).height() >= 48f * DENSITY);
    }

    @Test public void dpadDirectionsAndDiagonalAreResolvedWithoutOpposites() {
        GamepadHitMap map = GamepadHitMap.standard(2340, 1080, DENSITY, 0, 132, 0, 0);
        GamepadHitMap.Bounds d = map.dpadBounds();
        assertEquals(InputBits.UP, map.directionBits(d.centerX(), d.top + 8, 0));
        assertEquals(InputBits.DOWN, map.directionBits(d.centerX(), d.bottom - 8, 0));
        assertEquals(InputBits.LEFT, map.directionBits(d.left + 8, d.centerY(), 0));
        assertEquals(InputBits.RIGHT, map.directionBits(d.right - 8, d.centerY(), 0));
        assertEquals(InputBits.UP | InputBits.LEFT,
                map.directionBits(d.left + 12, d.top + 12, 0));
    }

    @Test public void dpadHysteresisRetainsDirectionNearThresholdThenCancelsOnExit() {
        GamepadHitMap map = GamepadHitMap.standard(2340, 1080, DENSITY, 0, 132, 0, 0);
        GamepadHitMap.Bounds d = map.dpadBounds();
        assertEquals(InputBits.RIGHT, map.directionBits(
                d.centerX() + 20f * DENSITY, d.centerY(), 0));
        assertEquals(InputBits.RIGHT, map.directionBits(
                d.centerX() + 13f * DENSITY, d.centerY(), InputBits.RIGHT));
        assertEquals(0, map.directionBits(d.right + 13f * DENSITY,
                d.centerY(), InputBits.RIGHT));
    }
}
