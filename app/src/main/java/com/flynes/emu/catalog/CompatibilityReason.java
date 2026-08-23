package com.flynes.emu.catalog;

/** Stable machine-readable compatibility reason; UI text is localized separately. */
public enum CompatibilityReason {
    PLAYABLE_NES,
    FDS_BIOS_API_NOT_IMPLEMENTED,
    UNIF_PRODUCT_DISABLED,
    NES_HEADER_INVALID,
    NES_ZERO_PRG,
    NES_TRUNCATED,
    NES_SIZE_OVERFLOW,
    FDS_INVALID_HEADER,
    FDS_INVALID_SIDE_COUNT,
    FDS_TRUNCATED,
    UNIF_INVALID_CHUNK,
    UNIF_MISSING_PRG,
    UNKNOWN_FORMAT
}
