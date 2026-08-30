package com.flynes.emu.video;

/** The native render-thread boundary that exclusively owns game-Surface frame-rate votes. */
public interface FrameRateRequestTarget {
    boolean requestFrameRate(long surfaceEpoch, float coreConfirmedSourceFps);
    boolean clearFrameRate(long surfaceEpoch);
}
