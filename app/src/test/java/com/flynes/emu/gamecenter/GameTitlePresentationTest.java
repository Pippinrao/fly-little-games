package com.flynes.emu.gamecenter;

import com.flynes.emu.catalog.CanonicalGame;

import org.junit.Test;

import java.util.List;
import java.util.Locale;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

public final class GameTitlePresentationTest {
    @Test public void chineseUiUsesChineseNameAndEnglishAsSecondary() {
        CanonicalGame game = new CanonicalGame(
                "contra", "Contra", "魂斗罗", List.of("Gryzor"));

        GameTitlePresentation.Title title = GameTitlePresentation.forLocale(
                game, Locale.SIMPLIFIED_CHINESE);

        assertEquals("魂斗罗", title.primary());
        assertEquals("Contra", title.secondary());
        assertFalse(title.usedFilenameFallback());
    }

    @Test public void englishUiUsesEnglishNameAndChineseAsSecondary() {
        CanonicalGame game = new CanonicalGame(
                "contra", "Contra", "魂斗罗", List.of());

        GameTitlePresentation.Title title = GameTitlePresentation.forLocale(
                game, Locale.ENGLISH);

        assertEquals("Contra", title.primary());
        assertEquals("魂斗罗", title.secondary());
        assertFalse(title.usedFilenameFallback());
    }

    @Test public void missingChineseNameKeepsReadableEnglishWithoutPretendingItWasTranslated() {
        CanonicalGame game = new CanonicalGame(
                "homebrew", "Nova the Squirrel", "", List.of());

        GameTitlePresentation.Title title = GameTitlePresentation.forLocale(
                game, Locale.SIMPLIFIED_CHINESE);

        assertEquals("Nova the Squirrel", title.primary());
        assertEquals("", title.secondary());
        assertFalse(title.usedFilenameFallback());
    }
}
