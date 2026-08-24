package com.flynes.emu.catalog.persistence;

public record CanonicalUserState(boolean favorite, long lastPlayedSequence, int playCount) {
    public static final CanonicalUserState EMPTY = new CanonicalUserState(false, 0, 0);

    public CanonicalUserState {
        if (lastPlayedSequence < 0 || playCount < 0) {
            throw new IllegalArgumentException("canonical user state must not be negative");
        }
    }

    public CanonicalUserState withFavorite(boolean value) {
        return new CanonicalUserState(value, lastPlayedSequence, playCount);
    }

    public CanonicalUserState launched(long sequence) {
        return new CanonicalUserState(favorite, sequence, Math.addExact(playCount, 1));
    }
}
