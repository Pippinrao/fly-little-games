package com.flynes.emu.settings;

import java.util.Locale;

public final class DisplayStatusText {
    private final float requestedHz;
    private final float actualHz;
    private final String fallbackReason;

    public DisplayStatusText(float requestedHz, float actualHz, String fallbackReason) {
        this.requestedHz = requestedHz;
        this.actualHz = actualHz;
        this.fallbackReason = fallbackReason == null ? "" : fallbackReason;
    }

    public String asText() {
        String base = String.format(Locale.ROOT, "Requested %s Hz · Actual %s Hz",
                number(requestedHz), number(actualHz));
        return fallbackReason.isEmpty() ? base : base + " · Fallback " + fallbackReason;
    }

    private static String number(float value) {
        if (Math.abs(value - Math.round(value)) < .005f) return Integer.toString(Math.round(value));
        return String.format(Locale.ROOT, "%.2f", value);
    }
}
