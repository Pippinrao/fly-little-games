package com.flynes.emu.video.quality;

import java.util.Collections;
import java.util.List;
import java.util.Map;

public final class AdaptiveQualityPolicy {
    private final List<AdaptiveTransition> downgradeTransitions;
    private final Map<String, Float> gpuP95BudgetMsByConfigurationId;
    private final float batteryDowngradeCelsius;
    private final float batterySafeCelsius;
    private final float batteryRecoveryCelsius;
    private final long recoveryStableMs;

    public AdaptiveQualityPolicy(List<AdaptiveTransition> downgradeTransitions,
                                 Map<String, Float> gpuP95BudgetMsByConfigurationId,
                                 float batteryDowngradeCelsius,
                                 float batterySafeCelsius,
                                 float batteryRecoveryCelsius,
                                 long recoveryStableMs) {
        this.downgradeTransitions = Collections.unmodifiableList(downgradeTransitions);
        this.gpuP95BudgetMsByConfigurationId = Collections.unmodifiableMap(
                gpuP95BudgetMsByConfigurationId);
        this.batteryDowngradeCelsius = batteryDowngradeCelsius;
        this.batterySafeCelsius = batterySafeCelsius;
        this.batteryRecoveryCelsius = batteryRecoveryCelsius;
        this.recoveryStableMs = recoveryStableMs;
    }

    public static AdaptiveQualityPolicy empty() {
        return new AdaptiveQualityPolicy(Collections.emptyList(), Collections.emptyMap(),
                40.0f, 42.0f, 38.0f, 120_000L);
    }

    public List<AdaptiveTransition> downgradeTransitions() { return downgradeTransitions; }
    public Map<String, Float> gpuP95BudgetMsByConfigurationId() {
        return gpuP95BudgetMsByConfigurationId;
    }
    public float batteryDowngradeCelsius() { return batteryDowngradeCelsius; }
    public float batterySafeCelsius() { return batterySafeCelsius; }
    public float batteryRecoveryCelsius() { return batteryRecoveryCelsius; }
    public long recoveryStableMs() { return recoveryStableMs; }
}
