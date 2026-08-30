package com.flynes.emu.video.quality;

import com.flynes.emu.video.power.ThermalBand;

import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.Objects;
import java.util.Set;

public final class RuntimeConstraints {
    private final SourceTiming sourceTiming;
    private final DisplayObservation displayObservation;
    private final long displayRequestGeneration;
    private final boolean systemBatterySaver;
    private final int batteryPercent;
    private final float batteryTemperatureCelsius;
    private final ThermalBand thermalBand;
    private final boolean optionalAdaptiveProtectionEnabled;
    private final Set<RuntimeFailure> runtimeFailures;
    private final RuntimeTemporalState currentTemporalState;

    public RuntimeConstraints(SourceTiming sourceTiming, DisplayObservation displayObservation,
                              boolean systemBatterySaver, int batteryPercent,
                              float batteryTemperatureCelsius, ThermalBand thermalBand,
                              boolean optionalAdaptiveProtectionEnabled,
                              Set<RuntimeFailure> runtimeFailures,
                              RuntimeTemporalState currentTemporalState) {
        this(sourceTiming, displayObservation,
                displayObservation == null ? 0L : displayObservation.requestGeneration(),
                systemBatterySaver, batteryPercent, batteryTemperatureCelsius, thermalBand,
                optionalAdaptiveProtectionEnabled, runtimeFailures, currentTemporalState);
    }

    public RuntimeConstraints(SourceTiming sourceTiming, DisplayObservation displayObservation,
                              long displayRequestGeneration,
                              boolean systemBatterySaver, int batteryPercent,
                              float batteryTemperatureCelsius, ThermalBand thermalBand,
                              boolean optionalAdaptiveProtectionEnabled,
                              Set<RuntimeFailure> runtimeFailures,
                              RuntimeTemporalState currentTemporalState) {
        this.sourceTiming = Objects.requireNonNull(sourceTiming, "sourceTiming");
        this.displayObservation = displayObservation;
        this.displayRequestGeneration = displayRequestGeneration;
        this.systemBatterySaver = systemBatterySaver;
        this.batteryPercent = batteryPercent;
        this.batteryTemperatureCelsius = batteryTemperatureCelsius;
        this.thermalBand = Objects.requireNonNull(thermalBand, "thermalBand");
        this.optionalAdaptiveProtectionEnabled = optionalAdaptiveProtectionEnabled;
        this.runtimeFailures = Collections.unmodifiableSet(new LinkedHashSet<>(
                Objects.requireNonNull(runtimeFailures, "runtimeFailures")));
        this.currentTemporalState = Objects.requireNonNull(currentTemporalState,
                "currentTemporalState");
    }
    public SourceTiming sourceTiming() { return sourceTiming; }
    public DisplayObservation displayObservation() { return displayObservation; }
    public long displayRequestGeneration() { return displayRequestGeneration; }
    public boolean systemBatterySaver() { return systemBatterySaver; }
    public int batteryPercent() { return batteryPercent; }
    public float batteryTemperatureCelsius() { return batteryTemperatureCelsius; }
    public ThermalBand thermalBand() { return thermalBand; }
    public boolean optionalAdaptiveProtectionEnabled() { return optionalAdaptiveProtectionEnabled; }
    public Set<RuntimeFailure> runtimeFailures() { return runtimeFailures; }
    public RuntimeTemporalState currentTemporalState() { return currentTemporalState; }
}
