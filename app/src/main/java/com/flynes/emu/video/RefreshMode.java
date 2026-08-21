package com.flynes.emu.video;

public enum RefreshMode {
    AUTO(0f),
    HZ_60(60f),
    HZ_90(90f),
    HZ_120(120f);

    private final float targetHz;

    RefreshMode(float targetHz) {
        this.targetHz = targetHz;
    }

    public float targetHz() {
        return targetHz;
    }
}
