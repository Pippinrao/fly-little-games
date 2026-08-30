package com.flynes.emu.catalog;

public record TitleCandidate(
        String value,
        Language language,
        Origin origin,
        Confidence confidence,
        ReviewState reviewState) {

    public TitleCandidate {
        value = DomainValidation.requireNonBlank(value, "title candidate");
        language = DomainValidation.requireNonNull(language, "title language");
        origin = DomainValidation.requireNonNull(origin, "title origin");
        confidence = DomainValidation.requireNonNull(confidence, "title confidence");
        reviewState = DomainValidation.requireNonNull(reviewState, "title review state");
    }

    public enum Language {
        EN,
        ZH_HANS,
        UNKNOWN
    }

    public enum Origin {
        OUTER_FILENAME,
        ZIP_ENTRY_NAME,
        BUILTIN_MANIFEST,
        MANUAL_OVERRIDE
    }

    public enum Confidence {
        LOW,
        HIGH,
        VERIFIED
    }

    public enum ReviewState {
        NEEDS_REVIEW,
        VERIFIED
    }
}
