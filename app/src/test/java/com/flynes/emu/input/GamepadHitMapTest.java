package com.flynes.emu.input;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.LayoutPreset;

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

    @Test
    public void maximumButtonScaleStillKeepsSixteenDpBetweenAAndB() {
        float density = 2.75f;
        AppSettings settings = AppSettings.defaults().toBuilder()
                .buttonScale(AppSettings.MAX_BUTTON_SCALE).build();
        GamepadHitMap map = GamepadHitMap.fromSettings(
                2340, 1080, density, 0, 132, 0, 0, settings);
        GamepadHitMap.Circle a = map.circle(GamepadHitMap.Control.A);
        GamepadHitMap.Circle b = map.circle(GamepadHitMap.Control.B);
        float gap = Math.abs(a.cx() - b.cx()) - a.radius() - b.radius();

        assertEquals(16f * density, gap, 0.01f);
        assertTrue(map.validate().toString(), map.validate().isEmpty());
    }

    @Test
    public void mirroredPresetPlacesAOnTheLeft() {
        AppSettings settings = AppSettings.defaults().toBuilder()
                .layoutPreset(LayoutPreset.MIRRORED_AB).build();
        GamepadHitMap map = GamepadHitMap.fromSettings(
                2340, 1080, 2.75f, 0, 132, 0, 0, settings);

        assertTrue(map.circle(GamepadHitMap.Control.A).cx()
                < map.circle(GamepadHitMap.Control.B).cx());
    }

    @Test
    public void longSystemLabelsUseCompactType() {
        assertEquals(9f, GamepadVisualMetrics.labelSizeDp(
                GamepadHitMap.Control.SELECT), 0f);
        assertEquals(9f, GamepadVisualMetrics.labelSizeDp(
                GamepadHitMap.Control.START), 0f);
        assertEquals(20f, GamepadVisualMetrics.labelSizeDp(
                GamepadHitMap.Control.A), 0f);
    }
}
