package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;

import com.flynes.emu.settings.AspectMode;

import org.junit.Test;

public final class ViewportLayoutTest {
    @Test public void fourByThreeUsesFullHeightWithoutEnteringNavigationInset() {
        ViewportLayout.Size size = ViewportLayout.compute(
                2340, 1080, 0, 132, AspectMode.FOUR_BY_THREE, 1024, 960);
        assertEquals(1440, size.width());
        assertEquals(1080, size.height());
    }

    @Test public void squarePixelsPreserveCoreAspect() {
        ViewportLayout.Size size = ViewportLayout.compute(
                2340, 1080, 0, 132, AspectMode.SQUARE_PIXELS, 1024, 960);
        assertEquals(1152, size.width());
        assertEquals(1080, size.height());
    }

    @Test public void integerScaleDoesNotExceedSafeArea() {
        ViewportLayout.Size size = ViewportLayout.compute(
                1200, 700, 0, 100, AspectMode.INTEGER_SCALE, 256, 240);
        assertEquals(512, size.width());
        assertEquals(480, size.height());
    }
}
