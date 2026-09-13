package com.flynes.emu.gamecenter;

import com.flynes.emu.catalog.CanonicalGame;

import org.junit.Test;

import java.util.List;
import java.util.Locale;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

public final class GameTitlePresentationTest {
    @Test public void explicitManualTitleWinsOverOtherVerifiedMetadata() {
        var manual = new com.flynes.emu.catalog.TitleCandidate("My title",
                com.flynes.emu.catalog.TitleCandidate.Language.EN,
                com.flynes.emu.catalog.TitleCandidate.Origin.MANUAL_OVERRIDE,
                com.flynes.emu.catalog.TitleCandidate.Confidence.VERIFIED,
                com.flynes.emu.catalog.TitleCandidate.ReviewState.VERIFIED);
        var builtin = new com.flynes.emu.catalog.TitleCandidate("Bundled title",
                com.flynes.emu.catalog.TitleCandidate.Language.EN,
                com.flynes.emu.catalog.TitleCandidate.Origin.BUILTIN_MANIFEST,
                com.flynes.emu.catalog.TitleCandidate.Confidence.VERIFIED,
                com.flynes.emu.catalog.TitleCandidate.ReviewState.VERIFIED);
        assertEquals("My title", GameTitlePresentation.forLocale(new CanonicalGame(
                "stable-id", List.of(builtin, manual), List.of()), Locale.ENGLISH).primary());
    }
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
