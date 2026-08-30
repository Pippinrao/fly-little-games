package com.flynes.emu.gamecenter;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.Locale;

public final class HomeHeaderLayoutPolicyTest {
    @Test public void reservesCategorySpaceForExpandedPseudoLanguageAndLargeText() {
        assertTrue(HomeHeaderLayoutPolicy.compact(1.0f, Locale.forLanguageTag("en-XA")));
        assertTrue(HomeHeaderLayoutPolicy.compact(1.8f, Locale.ENGLISH));
        assertFalse(HomeHeaderLayoutPolicy.compact(1.3f, Locale.SIMPLIFIED_CHINESE));
    }

    @Test public void selectedGameTitleCanWrapWhenLargeTextIsEnabled() {
        assertTrue(HomeHeaderLayoutPolicy.detailTitleMaxLines(2.0f) >= 2);
        assertTrue(HomeHeaderLayoutPolicy.detailTitleMaxLines(1.3f) == 1);
        assertTrue(HomeHeaderLayoutPolicy.launchButtonHeightDp(2.0f) >= 48);
        assertTrue(HomeHeaderLayoutPolicy.launchButtonHeightDp(2.0f) <= 88);
    }
}
