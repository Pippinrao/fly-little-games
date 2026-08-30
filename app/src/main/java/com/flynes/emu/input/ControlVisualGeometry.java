package com.flynes.emu.input;

public final class ControlVisualGeometry {
    private ControlVisualGeometry() { }
    public static float dpadArmPx(float density, float placementScale) {
        return 48f * density * placementScale;
    }
}
