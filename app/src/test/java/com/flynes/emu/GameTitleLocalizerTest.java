package com.flynes.emu;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

import java.util.Locale;

public final class GameTitleLocalizerTest {
    @Test public void filenamesAloneNeverGuessASeriesTitle() {
        assertEquals("Super Mario Bros. 3 (USA)",
                GameTitleLocalizer.localize("Super Mario Bros. 3 (USA)", Locale.SIMPLIFIED_CHINESE));
        assertEquals("Contra (J)",
                GameTitleLocalizer.localize("Contra (J)", Locale.SIMPLIFIED_CHINESE));
        assertEquals("The Legend of Zelda",
                GameTitleLocalizer.localize("The Legend of Zelda", Locale.SIMPLIFIED_CHINESE));
    }

    @Test public void englishAndUnknownNamesAreNeverRewritten() {
        assertEquals("Contra (J)", GameTitleLocalizer.localize("Contra (J)", Locale.ENGLISH));
        assertEquals("My Homebrew", GameTitleLocalizer.localize(
                "My Homebrew", Locale.SIMPLIFIED_CHINESE));
    }

    @Test public void existingChineseNamesAreNotDuplicated() {
        assertEquals("魂斗罗 (美版)", GameTitleLocalizer.localize(
                "魂斗罗 (美版)", Locale.SIMPLIFIED_CHINESE));
    }
}
