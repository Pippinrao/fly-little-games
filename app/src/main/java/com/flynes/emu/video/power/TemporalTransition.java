package com.flynes.emu.video.power;

import com.flynes.emu.video.quality.AdaptiveTriggerClass;
import com.flynes.emu.video.quality.RuntimeTemporalState;

import java.util.Objects;

public final class TemporalTransition {
    private final String fromConfigurationId;
    private final String toConfigurationId;
    private final RuntimeTemporalState fromTemporalState;
    private final RuntimeTemporalState toTemporalState;
    private final AdaptiveTriggerClass triggerClass;

    public TemporalTransition(String fromConfigurationId, String toConfigurationId,
                              RuntimeTemporalState fromTemporalState,
                              RuntimeTemporalState toTemporalState,
                              AdaptiveTriggerClass triggerClass) {
        this.fromConfigurationId = Objects.requireNonNull(
                fromConfigurationId, "fromConfigurationId");
        this.toConfigurationId = Objects.requireNonNull(toConfigurationId, "toConfigurationId");
        this.fromTemporalState = Objects.requireNonNull(
                fromTemporalState, "fromTemporalState");
        this.toTemporalState = Objects.requireNonNull(toTemporalState, "toTemporalState");
        this.triggerClass = Objects.requireNonNull(triggerClass, "triggerClass");
    }

    public String fromConfigurationId() { return fromConfigurationId; }
    public String toConfigurationId() { return toConfigurationId; }
    public RuntimeTemporalState fromTemporalState() { return fromTemporalState; }
    public RuntimeTemporalState toTemporalState() { return toTemporalState; }
    public AdaptiveTriggerClass triggerClass() { return triggerClass; }
}
