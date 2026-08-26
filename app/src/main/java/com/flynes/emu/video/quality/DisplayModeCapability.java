package com.flynes.emu.video.quality;

import java.util.Objects;

public final class DisplayModeCapability {
    private final int modeId;
    private final int width;
    private final int height;
    private final int refreshMilliHz;

    public DisplayModeCapability(int modeId, int width, int height, int refreshMilliHz) {
        if (modeId <= 0 || width <= 0 || height <= 0 || refreshMilliHz <= 0)
            throw new IllegalArgumentException("display mode values must be positive");
        this.modeId = modeId;
        this.width = width;
        this.height = height;
        this.refreshMilliHz = refreshMilliHz;
    }
    public int modeId() { return modeId; }
    public int width() { return width; }
    public int height() { return height; }
    public int refreshMilliHz() { return refreshMilliHz; }
    @Override public boolean equals(Object object) {
        if (this == object) return true;
        if (!(object instanceof DisplayModeCapability)) return false;
        DisplayModeCapability other = (DisplayModeCapability) object;
        return modeId == other.modeId && width == other.width && height == other.height
                && refreshMilliHz == other.refreshMilliHz;
    }
    @Override public int hashCode() { return Objects.hash(modeId, width, height, refreshMilliHz); }
}
