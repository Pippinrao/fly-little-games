package com.flynes.emu.video.quality;

public enum RuntimeTemporalState {
    REQUESTED,
    APPLIED,
    FALLBACK,
    IMMEDIATE_NATIVE,
    PRIMING,
    PRIMING_SHADOW,
    MOTION_COMPENSATING,
    BUFFERED_NATIVE_HOLD,
    SURFACE_SUSPENDED_HOLD,
    DRAINING
}
