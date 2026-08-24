package com.flynes.emu.settings;

public record DisplayStatus(float requestedHz, float actualHz, String fallbackReason) {
    public DisplayStatus {
        if (requestedHz < 0f || actualHz < 0f) throw new IllegalArgumentException("invalid refresh rate");
        fallbackReason = fallbackReason == null ? "" : fallbackReason;
    }
}
