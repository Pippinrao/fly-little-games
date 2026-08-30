package com.flynes.emu.video;

import java.util.Objects;

public final class DisplayModeSelector {
    private static final float REFRESH_TOLERANCE_HZ = 0.6f;

    private DisplayModeSelector() { }

    public static int select(DisplayCandidate[] modes, int nativeWidth, int nativeHeight,
                             RefreshMode requested) {
        Objects.requireNonNull(modes, "modes");
        Objects.requireNonNull(requested, "requested");
        if (requested == RefreshMode.AUTO) {
            int preferred120 = selectTarget(modes, nativeWidth, nativeHeight, 120f);
            return preferred120 != 0
                    ? preferred120 : selectTarget(modes, nativeWidth, nativeHeight, 60f);
        }
        return selectTarget(modes, nativeWidth, nativeHeight, requested.targetHz());
    }

    private static int selectTarget(DisplayCandidate[] modes, int nativeWidth,
                                    int nativeHeight, float targetHz) {
        DisplayCandidate best = null;
        for (DisplayCandidate mode : modes) {
            if (mode == null || !sameResolution(mode, nativeWidth, nativeHeight)) continue;
            if (Math.abs(mode.refreshRate() - targetHz) > REFRESH_TOLERANCE_HZ) continue;
            if (best == null || Math.abs(mode.refreshRate() - targetHz)
                    < Math.abs(best.refreshRate() - targetHz)) best = mode;
        }
        return best == null ? 0 : best.modeId();
    }

    private static boolean sameResolution(DisplayCandidate mode, int width, int height) {
        return (mode.width() == width && mode.height() == height)
                || (mode.width() == height && mode.height() == width);
    }
}
