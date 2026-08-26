package com.flynes.emu.video.quality;

import java.util.Objects;

public final class DisplayObservation {
    private final long requestGeneration;
    private final PhysicalRefreshPolicy requestedPolicy;
    private final DisplayModeCapability requestedMode;
    private final DisplayModeCapability systemReportedActiveMode;
    private final long observedAtElapsedRealtimeMs;
    private final long stableForMs;

    public DisplayObservation(long requestGeneration, PhysicalRefreshPolicy requestedPolicy,
                              DisplayModeCapability requestedMode,
                              DisplayModeCapability systemReportedActiveMode,
                              long observedAtElapsedRealtimeMs, long stableForMs) {
        this.requestGeneration = requestGeneration;
        this.requestedPolicy = Objects.requireNonNull(requestedPolicy, "requestedPolicy");
        this.requestedMode = requestedMode;
        this.systemReportedActiveMode = systemReportedActiveMode;
        this.observedAtElapsedRealtimeMs = observedAtElapsedRealtimeMs;
        this.stableForMs = stableForMs;
    }
    public long requestGeneration() { return requestGeneration; }
    public PhysicalRefreshPolicy requestedPolicy() { return requestedPolicy; }
    public DisplayModeCapability requestedMode() { return requestedMode; }
    public DisplayModeCapability systemReportedActiveMode() { return systemReportedActiveMode; }
    public long observedAtElapsedRealtimeMs() { return observedAtElapsedRealtimeMs; }
    public long stableForMs() { return stableForMs; }
}
