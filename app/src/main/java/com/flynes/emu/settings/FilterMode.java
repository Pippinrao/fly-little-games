package com.flynes.emu.settings;

public enum FilterMode {
    /** Edge-aware Scale2x-style GPU reconstruction for pixel art. */
    EDGE_ENHANCED,
    /** Texel-aware bilinear filtering that keeps pixel centres crisp. */
    SHARP_BILINEAR,
    NEAREST,
    CRT,
    /** Qualified MMPX 2x reconstruction followed by sharp final composition. */
    MMPX,
    /** Qualified standard ScaleFX five-pass 3x reconstruction. */
    SCALEFX
}
