package com.flynes.emu.input;

import static org.junit.Assert.assertEquals;
import org.junit.Test;

public final class ControlVisualGeometryTest {
    @Test public void dpadArmTracksPlacementScaleAtBothValidatedExtremes() {
        assertEquals(24f, ControlVisualGeometry.dpadArmPx(1f, .5f), .001f);
        assertEquals(86.4f, ControlVisualGeometry.dpadArmPx(1f, 1.8f), .001f);
        assertEquals(172.8f, ControlVisualGeometry.dpadArmPx(2f, 1.8f), .001f);
    }
}
