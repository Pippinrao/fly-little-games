package com.flynes.emu.catalog;

import java.util.Collections;
import java.util.List;

/** Parsed, non-user-facing structural metadata for a ROM payload. */
public record RomAnalysis(
        long expectedBytes,
        long actualBytes,
        long prgBytes,
        long chrBytes,
        int mapper,
        int submapper,
        boolean trainer,
        boolean battery,
        int diskSides,
        List<Warning> warnings) {

    public RomAnalysis {
        if (expectedBytes < 0 || actualBytes < 0 || prgBytes < 0 || chrBytes < 0) {
            throw new IllegalArgumentException("ROM byte counts must not be negative");
        }
        if (mapper < -1 || submapper < -1 || diskSides < 0) {
            throw new IllegalArgumentException("ROM metadata numeric values are invalid");
        }
        warnings = DomainValidation.immutableList(warnings, "ROM warnings");
    }

    public static RomAnalysis basic(long actualBytes) {
        return new RomAnalysis(
                actualBytes, actualBytes, 0, 0, -1, -1,
                false, false, 0, Collections.<Warning>emptyList());
    }

    public enum Warning {
        TRAILING_DATA,
        DIRTY_HEADER,
        UNICODE_PATH_REJECTED
    }
}
