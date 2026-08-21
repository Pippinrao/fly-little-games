package com.flynes.emu.video;

import java.util.Objects;

public final class DisplayModeSelector {
    private static final float REFRESH_TOLERANCE_HZ = 0.6f;

    private DisplayModeSelector() { }

    public static int select(DisplayCandidate[] modes, int nativeWidth, int nativeHeight,
                             RefreshMode requested) {
        Objects.requireNonNull(modes, "modes");
        Objects.requireNonNull(requested, "requested");
        DisplayCandidate best = null;
        for (DisplayCandidate mode : modes) {
            if (mode == null || !sameResolution(mode, nativeWidth, nativeHeight)) continue;
            if (requested != RefreshMode.AUTO
                    && Math.abs(mode.refreshRate() - requested.targetHz())
                    > REFRESH_TOLERANCE_HZ) {
                continue;
            }
            if (best == null || mode.refreshRate() > best.refreshRate()) best = mode;
        }
        return best == null ? 0 : best.modeId();
    }

    private static boolean sameResolution(DisplayCandidate mode, int width, int height) {
        return (mode.width() == width && mode.height() == height)
                || (mode.width() == height && mode.height() == width);
    }
}
