package com.flynes.emu.catalog.persistence;

public record CanonicalUserState(
        boolean favorite,
        long favoriteUpdatedRevision,
        long lastPlayedSequence,
        int playCount) {
    public static final CanonicalUserState EMPTY = new CanonicalUserState(false, 0, 0, 0);

    public CanonicalUserState {
        if (favoriteUpdatedRevision < 0 || lastPlayedSequence < 0 || playCount < 0) {
            throw new IllegalArgumentException("canonical user state must not be negative");
        }
    }

    public CanonicalUserState withFavorite(boolean value, long revision) {
        if (revision < favoriteUpdatedRevision) {
            throw new IllegalArgumentException("favorite revision must be monotonic");
        }
        return new CanonicalUserState(value, revision, lastPlayedSequence, playCount);
    }

    public CanonicalUserState launched(long sequence) {
        return new CanonicalUserState(
                favorite, favoriteUpdatedRevision, sequence, Math.addExact(playCount, 1));
    }
}
