package com.flynes.emu.video.quality;

import java.util.Collections;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

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
        this.downgradeTransitions = Collections.unmodifiableList(new ArrayList<>(
                Objects.requireNonNull(downgradeTransitions, "downgradeTransitions")));
        this.gpuP95BudgetMsByConfigurationId = Collections.unmodifiableMap(new LinkedHashMap<>(
                Objects.requireNonNull(gpuP95BudgetMsByConfigurationId,
                        "gpuP95BudgetMsByConfigurationId")));
        if (recoveryStableMs < 0L) {
            throw new IllegalArgumentException("recoveryStableMs must be non-negative");
        }
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
