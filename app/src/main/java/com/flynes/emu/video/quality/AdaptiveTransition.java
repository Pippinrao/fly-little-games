package com.flynes.emu.video.quality;

import java.util.Objects;

public final class AdaptiveTransition {
    private final String fromConfigurationId;
    private final String toConfigurationId;
    private final AdaptiveTriggerClass triggerClass;

    public AdaptiveTransition(String fromConfigurationId, String toConfigurationId,
                              AdaptiveTriggerClass triggerClass) {
        this.fromConfigurationId = Objects.requireNonNull(
                fromConfigurationId, "fromConfigurationId");
        this.toConfigurationId = Objects.requireNonNull(toConfigurationId, "toConfigurationId");
        this.triggerClass = Objects.requireNonNull(triggerClass, "triggerClass");
    }

    public String fromConfigurationId() { return fromConfigurationId; }
    public String toConfigurationId() { return toConfigurationId; }
    public AdaptiveTriggerClass triggerClass() { return triggerClass; }
}
