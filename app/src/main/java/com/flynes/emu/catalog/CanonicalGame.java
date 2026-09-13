package com.flynes.emu.catalog;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/** A release-level work. Payload identity belongs to each variant, not to this metadata group. */
public record CanonicalGame(
        String id,
        List<TitleCandidate> titleCandidates,
        List<String> aliases) {

    private static final Comparator<TitleCandidate> TITLE_PREFERENCE = Comparator
            .comparingInt((TitleCandidate value) ->
                    value.origin() == TitleCandidate.Origin.MANUAL_OVERRIDE ? 0 : 1)
            .thenComparing(Comparator.comparingInt(
                    (TitleCandidate value) -> value.reviewState().ordinal()).reversed())
            .thenComparing(Comparator.comparingInt(
                    (TitleCandidate value) -> value.confidence().ordinal()).reversed())
            .thenComparing(TitleCandidate::origin)
            .thenComparing(TitleCandidate::value);

    public CanonicalGame {
        id = DomainValidation.requireNonBlank(id, "canonical game id");
        titleCandidates = DomainValidation.immutableList(titleCandidates, "title candidates");
        DomainValidation.requireNonNull(aliases, "aliases");
        ArrayList<String> ownedAliases = new ArrayList<>(aliases.size());
        for (String alias : aliases) {
            ownedAliases.add(DomainValidation.requireNonBlank(alias, "alias"));
        }
        aliases = Collections.unmodifiableList(ownedAliases);
    }

    /** Convenience for trusted built-in/manual metadata. Scanner filenames use LOW candidates. */
    public CanonicalGame(
            String id,
            String englishTitle,
            String zhHansTitle,
            List<String> aliases) {
        this(id, trustedCandidates(englishTitle, zhHansTitle), aliases);
    }

    public String englishTitle() {
        return preferred(TitleCandidate.Language.EN);
    }

    public String zhHansTitle() {
        return preferred(TitleCandidate.Language.ZH_HANS);
    }

    private String preferred(TitleCandidate.Language language) {
        TitleCandidate preferred = null;
        for (TitleCandidate candidate : titleCandidates) {
            if (candidate.language() == language
                    && (preferred == null || TITLE_PREFERENCE.compare(candidate, preferred) < 0)) {
                preferred = candidate;
            }
        }
        return preferred == null ? "" : preferred.value();
    }

    private static List<TitleCandidate> trustedCandidates(String english, String chinese) {
        ArrayList<TitleCandidate> candidates = new ArrayList<>(2);
        if (!DomainValidation.isBlank(english)) {
            candidates.add(new TitleCandidate(
                    english,
                    TitleCandidate.Language.EN,
                    TitleCandidate.Origin.BUILTIN_MANIFEST,
                    TitleCandidate.Confidence.VERIFIED,
                    TitleCandidate.ReviewState.VERIFIED));
        }
        if (!DomainValidation.isBlank(chinese)) {
            candidates.add(new TitleCandidate(
                    chinese,
                    TitleCandidate.Language.ZH_HANS,
                    TitleCandidate.Origin.BUILTIN_MANIFEST,
                    TitleCandidate.Confidence.VERIFIED,
                    TitleCandidate.ReviewState.VERIFIED));
        }
        return candidates;
    }
}
