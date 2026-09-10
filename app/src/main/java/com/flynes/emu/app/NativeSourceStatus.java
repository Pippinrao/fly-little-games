package com.flynes.emu.app;

import java.util.Arrays;
import java.util.Objects;

public record NativeSourceStatus(
        byte[] sourceUuid, int sourceScope, int lastCompleteness, int freshness) {
    public NativeSourceStatus {
        sourceUuid = Objects.requireNonNull(sourceUuid, "source uuid").clone();
    }

    @Override public byte[] sourceUuid() { return sourceUuid.clone(); }

    @Override public boolean equals(Object other) {
        if (this == other) return true;
        if (!(other instanceof NativeSourceStatus that)) return false;
        return sourceScope == that.sourceScope && lastCompleteness == that.lastCompleteness
                && freshness == that.freshness && Arrays.equals(sourceUuid, that.sourceUuid);
    }

    @Override public int hashCode() {
        return Objects.hash(sourceScope, lastCompleteness, freshness, Arrays.hashCode(sourceUuid));
    }
}
