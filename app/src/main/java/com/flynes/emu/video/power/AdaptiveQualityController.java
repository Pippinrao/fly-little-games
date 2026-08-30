package com.flynes.emu.video.power;

import com.flynes.emu.video.quality.AdaptiveQualityPolicy;
import com.flynes.emu.video.quality.AdaptiveTransition;
import com.flynes.emu.video.quality.AdaptiveTriggerClass;
import com.flynes.emu.video.quality.RuntimeTemporalState;

import java.util.ArrayDeque;
import java.util.Deque;
import java.util.Objects;

/** Stateful policy graph controller. It never invents configurations outside the profile. */
public final class AdaptiveQualityController {
    private final AdaptiveQualityPolicy policy;
    private final Deque<AdaptiveTransition> recoveryStack = new ArrayDeque<>();
    private String targetConfigurationId;
    private Long recoveryBecameSafeAtElapsedMs;

    public AdaptiveQualityController(AdaptiveQualityPolicy policy,
                                     String initialConfigurationId) {
        this.policy = Objects.requireNonNull(policy, "policy");
        this.targetConfigurationId = Objects.requireNonNull(
                initialConfigurationId, "initialConfigurationId");
    }

    public String targetConfigurationId() { return targetConfigurationId; }

    public TemporalTransition update(ThermalPowerSnapshot snapshot,
                                     boolean optionalAdaptiveProtectionEnabled,
                                     RuntimeTemporalState currentTemporalState,
                                     boolean recoveryBoundaryAvailable) {
        Objects.requireNonNull(snapshot, "snapshot");
        Objects.requireNonNull(currentTemporalState, "currentTemporalState");

        if (snapshot.thermalBand() == ThermalBand.CRITICAL) {
            recoveryBecameSafeAtElapsedMs = null;
            return new TemporalTransition(targetConfigurationId, targetConfigurationId,
                    currentTemporalState, RuntimeTemporalState.SURFACE_SUSPENDED_HOLD,
                    AdaptiveTriggerClass.CRITICAL_THERMAL);
        }

        AdaptiveTriggerClass trigger = downgradeTrigger(snapshot,
                optionalAdaptiveProtectionEnabled);
        if (trigger != null) {
            recoveryBecameSafeAtElapsedMs = null;
            AdaptiveTransition edge = findEdge(targetConfigurationId, trigger);
            if (edge == null) return null;
            String from = targetConfigurationId;
            targetConfigurationId = edge.toConfigurationId();
            recoveryStack.push(edge);
            RuntimeTemporalState targetState = currentTemporalState
                    == RuntimeTemporalState.MOTION_COMPENSATING
                    ? RuntimeTemporalState.BUFFERED_NATIVE_HOLD
                    : RuntimeTemporalState.IMMEDIATE_NATIVE;
            return new TemporalTransition(from, targetConfigurationId, currentTemporalState,
                    targetState, trigger);
        }

        if (!recoveryConditionsMet(snapshot)) {
            recoveryBecameSafeAtElapsedMs = null;
            return null;
        }
        if (recoveryStack.isEmpty()) return null;
        if (recoveryBecameSafeAtElapsedMs == null) {
            recoveryBecameSafeAtElapsedMs = snapshot.sampledAtElapsedRealtimeMs();
            return null;
        }
        long stableFor;
        try {
            stableFor = Math.subtractExact(snapshot.sampledAtElapsedRealtimeMs(),
                    recoveryBecameSafeAtElapsedMs);
        } catch (ArithmeticException overflow) {
            recoveryBecameSafeAtElapsedMs = null;
            return null;
        }
        if (stableFor < policy.recoveryStableMs() || !recoveryBoundaryAvailable) return null;

        AdaptiveTransition recoveredEdge = recoveryStack.pop();
        String from = targetConfigurationId;
        targetConfigurationId = recoveredEdge.fromConfigurationId();
        recoveryBecameSafeAtElapsedMs = null;
        return new TemporalTransition(from, targetConfigurationId, currentTemporalState,
                RuntimeTemporalState.IMMEDIATE_NATIVE, recoveredEdge.triggerClass());
    }

    private AdaptiveTriggerClass downgradeTrigger(ThermalPowerSnapshot snapshot,
                                                  boolean optionalEnabled) {
        if (snapshot.thermalBand() == ThermalBand.SEVERE
                || snapshot.batteryTemperatureCelsius() >= policy.batterySafeCelsius()) {
            return AdaptiveTriggerClass.THERMAL_OR_BATTERY_TEMPERATURE;
        }
        if (snapshot.systemBatterySaver()) {
            return AdaptiveTriggerClass.SYSTEM_BATTERY_SAVER;
        }
        if (snapshot.batteryPercent() <= 15) return AdaptiveTriggerClass.LOW_BATTERY;
        Float budget = policy.gpuP95BudgetMsByConfigurationId().get(targetConfigurationId);
        boolean gpuOverBudget = budget != null && Float.isFinite(snapshot.gpuP95Ms())
                && snapshot.gpuP95Ms() > budget;
        if (optionalEnabled && (snapshot.thermalBand() == ThermalBand.MODERATE
                || snapshot.batteryTemperatureCelsius() >= policy.batteryDowngradeCelsius()
                || gpuOverBudget)) {
            return AdaptiveTriggerClass.OPTIONAL_ADAPTIVE_PROTECTION;
        }
        return null;
    }

    private boolean recoveryConditionsMet(ThermalPowerSnapshot snapshot) {
        Float budget = policy.gpuP95BudgetMsByConfigurationId().get(targetConfigurationId);
        boolean gpuSafe = budget == null || !Float.isFinite(snapshot.gpuP95Ms())
                || snapshot.gpuP95Ms() <= budget;
        return snapshot.thermalBand().ordinal() <= ThermalBand.LIGHT.ordinal()
                && !snapshot.systemBatterySaver() && snapshot.batteryPercent() > 20
                && snapshot.batteryTemperatureCelsius() < policy.batteryRecoveryCelsius()
                && gpuSafe;
    }

    private AdaptiveTransition findEdge(String from, AdaptiveTriggerClass trigger) {
        AdaptiveTransition result = null;
        for (AdaptiveTransition transition : policy.downgradeTransitions()) {
            if (from.equals(transition.fromConfigurationId())
                    && trigger == transition.triggerClass()) {
                if (result != null) {
                    throw new IllegalStateException("Ambiguous adaptive transition for "
                            + from + " and " + trigger);
                }
                result = transition;
            }
        }
        return result;
    }
}
