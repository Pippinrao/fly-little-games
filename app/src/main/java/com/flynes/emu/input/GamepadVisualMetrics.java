package com.flynes.emu.input;

/** Visual sizing that keeps long system-button labels inside their targets. */
public final class GamepadVisualMetrics {
    private GamepadVisualMetrics() { }

    public static float labelSizeDp(GamepadHitMap.Control control) {
        return control == GamepadHitMap.Control.SELECT
                || control == GamepadHitMap.Control.START ? 9f : 20f;
    }
}
