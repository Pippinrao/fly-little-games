package com.flynes.emu.video.platform;

import java.util.List;

public interface DisplayPlatformFacade {
    final class Mode {
        private final int modeId;
        private final int width;
        private final int height;
        private final float refreshHz;

        public Mode(int modeId, int width, int height, float refreshHz) {
            if (modeId <= 0 || width <= 0 || height <= 0
                    || !Float.isFinite(refreshHz) || refreshHz <= 0.0f) {
                throw new IllegalArgumentException("invalid platform display mode");
            }
            this.modeId = modeId;
            this.width = width;
            this.height = height;
            this.refreshHz = refreshHz;
        }

        public int modeId() { return modeId; }
        public int width() { return width; }
        public int height() { return height; }
        public float refreshHz() { return refreshHz; }
    }

    Mode currentMode();
    List<Mode> supportedModes();
    default void setPreferredDisplayModeId(int modeId) {
        throw new UnsupportedOperationException("display requests are unavailable");
    }
}
