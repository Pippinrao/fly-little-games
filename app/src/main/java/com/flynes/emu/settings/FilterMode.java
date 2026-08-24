package com.flynes.emu.settings;

public enum FilterMode {
    /** Edge-aware Scale2x-style GPU reconstruction for pixel art. */
    EDGE_ENHANCED,
    /** Texel-aware bilinear filtering that keeps pixel centres crisp. */
    SHARP_BILINEAR,
    NEAREST,
    CRT
}
