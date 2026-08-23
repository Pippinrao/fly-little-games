package com.flynes.emu.catalog;

public record CompatibilityDecision(
        CompatibilityState state,
        CompatibilityReason reason) {

    public CompatibilityDecision {
        state = DomainValidation.requireNonNull(state, "compatibility state");
        reason = DomainValidation.requireNonNull(reason, "compatibility reason");
        if (state == CompatibilityState.PLAYABLE && reason != CompatibilityReason.PLAYABLE_NES) {
            throw new IllegalArgumentException("only a structurally valid NES is playable");
        }
    }

    public static CompatibilityDecision playableNes() {
        return new CompatibilityDecision(
                CompatibilityState.PLAYABLE, CompatibilityReason.PLAYABLE_NES);
    }

    public boolean isPlayable() {
        return state == CompatibilityState.PLAYABLE;
    }
}
