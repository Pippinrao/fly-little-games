package com.flynes.emu.catalog;

public enum CompatibilityState {
    PLAYABLE,
    UNSUPPORTED,
    INVALID,
    UNKNOWN;

    public boolean isPlayable() {
        return this == PLAYABLE;
    }
}
