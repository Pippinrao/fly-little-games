package com.flynes.emu.gamecenter;

import com.flynes.emu.catalog.CanonicalGame;
import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.TitleCandidate;

import java.util.Locale;

/** Chooses readable catalog titles without exposing package or ROM filenames as metadata. */
public final class GameTitlePresentation {
    private GameTitlePresentation() { }

    public static Title forLocale(CanonicalGame game, Locale locale) {
        DomainValidation.requireNonNull(game, "canonical game");
        boolean chinese = locale != null && "zh".equals(locale.getLanguage());
        String preferred = chinese ? game.zhHansTitle() : game.englishTitle();
        String secondary = chinese ? game.englishTitle() : game.zhHansTitle();
        if (!preferred.isEmpty()) {
            return new Title(preferred, distinct(secondary, preferred), false);
        }
        if (!secondary.isEmpty()) {
            return new Title(secondary, "", false);
        }
        String unclassified = unclassifiedTitle(game);
        if (!unclassified.isEmpty()) {
            return new Title(unclassified, "", true);
        }
        return new Title(chinese ? "未命名游戏" : "Untitled game", "", false);
    }

    private static String unclassifiedTitle(CanonicalGame game) {
        for (TitleCandidate candidate : game.titleCandidates()) {
            if (candidate.language() == TitleCandidate.Language.UNKNOWN) {
                return candidate.value();
            }
        }
        return "";
    }

    private static String distinct(String candidate, String primary) {
        return candidate.isEmpty() || candidate.equals(primary) ? "" : candidate;
    }

    public record Title(String primary, String secondary, boolean usedFilenameFallback) { }
}
