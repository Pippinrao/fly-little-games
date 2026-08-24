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

    public static CompatibilityDecision requireValidFor(
            RomFormat format, CompatibilityDecision decision) {
        format = DomainValidation.requireNonNull(format, "ROM format");
        decision = DomainValidation.requireNonNull(decision, "compatibility decision");
        boolean valid = switch (format) {
            case INES, NES2 -> (decision.state == CompatibilityState.PLAYABLE
                    && decision.reason == CompatibilityReason.PLAYABLE_NES)
                    || (decision.state == CompatibilityState.INVALID
                    && isNesInvalidReason(decision.reason));
            case FDS -> (decision.state == CompatibilityState.UNSUPPORTED
                    && decision.reason == CompatibilityReason.FDS_BIOS_API_NOT_IMPLEMENTED)
                    || (decision.state == CompatibilityState.INVALID
                    && isFdsInvalidReason(decision.reason));
            case UNIF -> (decision.state == CompatibilityState.UNSUPPORTED
                    && decision.reason == CompatibilityReason.UNIF_PRODUCT_DISABLED)
                    || (decision.state == CompatibilityState.INVALID
                    && isUnifInvalidReason(decision.reason));
            case UNKNOWN -> false;
        };
        if (!valid) {
            throw new IllegalArgumentException(
                    "ROM format and compatibility decision are inconsistent");
        }
        return decision;
    }

    private static boolean isNesInvalidReason(CompatibilityReason reason) {
        return reason == CompatibilityReason.NES_HEADER_INVALID
                || reason == CompatibilityReason.NES_ZERO_PRG
                || reason == CompatibilityReason.NES_TRUNCATED
                || reason == CompatibilityReason.NES_SIZE_OVERFLOW;
    }

    private static boolean isFdsInvalidReason(CompatibilityReason reason) {
        return reason == CompatibilityReason.FDS_INVALID_HEADER
                || reason == CompatibilityReason.FDS_INVALID_SIDE_COUNT
                || reason == CompatibilityReason.FDS_TRUNCATED;
    }

    private static boolean isUnifInvalidReason(CompatibilityReason reason) {
        return reason == CompatibilityReason.UNIF_INVALID_CHUNK
                || reason == CompatibilityReason.UNIF_MISSING_PRG;
    }
}
