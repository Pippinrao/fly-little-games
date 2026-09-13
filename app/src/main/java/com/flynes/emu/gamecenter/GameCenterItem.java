package com.flynes.emu.gamecenter;

import com.flynes.emu.catalog.DomainValidation;

/** Small immutable projection used by the Game Center; it never contains ROM bytes or locators. */
public record GameCenterItem(
        String canonicalId,
        String titleEn,
        String titleZhHans,
        boolean builtin,
        boolean favorite,
        long lastPlayedSequence,
        String originalFilename,
        int popularityScore) {
    public GameCenterItem(String canonicalId, String titleEn, String titleZhHans,
                          boolean builtin, boolean favorite, long lastPlayedSequence,
                          String originalFilename) {
        this(canonicalId, titleEn, titleZhHans, builtin, favorite, lastPlayedSequence,
                originalFilename, -1);
    }
    public GameCenterItem {
        canonicalId = DomainValidation.requireNonBlank(canonicalId, "canonical id");
        titleEn = titleEn == null ? "" : titleEn;
        titleZhHans = titleZhHans == null ? "" : titleZhHans;
        originalFilename = originalFilename == null ? "" : originalFilename;
        if (popularityScore < -1 || popularityScore > 100) throw new IllegalArgumentException("popularity score");
        if (lastPlayedSequence < 0) {
            throw new IllegalArgumentException("last played sequence must not be negative");
        }
    }
}
