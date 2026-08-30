package com.flynes.emu.video.quality;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

public final class DisplayCapabilities {
    private final int currentWidth;
    private final int currentHeight;
    private final List<DisplayModeCapability> sameResolutionModes;
    private final GlCapabilities gl;

    public DisplayCapabilities(int currentWidth, int currentHeight,
                               List<DisplayModeCapability> sameResolutionModes,
                               GlCapabilities gl) {
        if (currentWidth <= 0 || currentHeight <= 0)
            throw new IllegalArgumentException("display dimensions must be positive");
        this.currentWidth = currentWidth;
        this.currentHeight = currentHeight;
        this.sameResolutionModes = Collections.unmodifiableList(new ArrayList<>(
                Objects.requireNonNull(sameResolutionModes, "sameResolutionModes")));
        this.gl = Objects.requireNonNull(gl, "gl");
    }
    public int currentWidth() { return currentWidth; }
    public int currentHeight() { return currentHeight; }
    public List<DisplayModeCapability> sameResolutionModes() { return sameResolutionModes; }
    public GlCapabilities gl() { return gl; }
}
