package com.flynes.emu.catalog;

import java.util.List;

public record GameCatalogEntry(
        CanonicalGame canonicalGame,
        List<GameVariant> variants,
        boolean favorite,
        long lastPlayedSequence,
        int playCount) {

    public GameCatalogEntry {
        canonicalGame = DomainValidation.requireNonNull(canonicalGame, "canonical game");
        variants = DomainValidation.immutableList(variants, "variants");
        if (lastPlayedSequence < 0) {
            throw new IllegalArgumentException("last played sequence must not be negative");
        }
        if (playCount < 0) {
            throw new IllegalArgumentException("play count must not be negative");
        }
        for (GameVariant variant : variants) {
            DomainValidation.requireNonNull(variant, "variant");
            if (!canonicalGame.id().equals(variant.canonicalGameId())) {
                throw new IllegalArgumentException(
                        "variant canonical game id does not match catalog entry");
            }
        }
    }

    public boolean isRecent() {
        return lastPlayedSequence > 0;
    }

    GameCatalogEntry withFavorite(boolean newFavorite) {
        return new GameCatalogEntry(
                canonicalGame, variants, newFavorite, lastPlayedSequence, playCount);
    }

    GameCatalogEntry withSuccessfulLaunch(long sequence) {
        return new GameCatalogEntry(
                canonicalGame, variants, favorite, sequence, Math.addExact(playCount, 1));
    }
}
