package com.flynes.emu.video.power;

import java.util.Objects;

public final class ThermalPowerSnapshot {
    private final ThermalBand thermalBand;
    private final boolean systemBatterySaver;
    private final int batteryPercent;
    private final float batteryTemperatureCelsius;
    private final float gpuP95Ms;
    private final long sampledAtElapsedRealtimeMs;

    public ThermalPowerSnapshot(ThermalBand thermalBand, boolean systemBatterySaver,
                                int batteryPercent, float batteryTemperatureCelsius,
                                float gpuP95Ms, long sampledAtElapsedRealtimeMs) {
        this.thermalBand = Objects.requireNonNull(thermalBand, "thermalBand");
        this.systemBatterySaver = systemBatterySaver;
        this.batteryPercent = batteryPercent;
        this.batteryTemperatureCelsius = batteryTemperatureCelsius;
        this.gpuP95Ms = gpuP95Ms;
        this.sampledAtElapsedRealtimeMs = sampledAtElapsedRealtimeMs;
    }

    public ThermalBand thermalBand() { return thermalBand; }
    public boolean systemBatterySaver() { return systemBatterySaver; }
    public int batteryPercent() { return batteryPercent; }
    public float batteryTemperatureCelsius() { return batteryTemperatureCelsius; }
    public float gpuP95Ms() { return gpuP95Ms; }
    public long sampledAtElapsedRealtimeMs() { return sampledAtElapsedRealtimeMs; }
}
