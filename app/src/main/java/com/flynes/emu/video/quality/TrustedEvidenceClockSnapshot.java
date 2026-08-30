package com.flynes.emu.video.quality;

public final class TrustedEvidenceClockSnapshot {
    private final BootSessionIdentity currentBootSession;
    private final PersistedEvidenceClockState persistedState;
    private final PersistedEvidenceClockAnchor persistedAnchor;
    private final AuthenticatedTimeSample authenticatedNetworkTime;
    private final long systemWallEpochMs;
    private final long elapsedRealtimeMs;
    private final long releaseBuildEpochMs;
    private final Long authenticatedNetworkNowEpochMs;
    private final Long monotonicFloorEpochMs;
    private final long evaluatedAtEpochMs;
    private final EvidenceClockTrust trust;

    public TrustedEvidenceClockSnapshot(BootSessionIdentity currentBootSession,
                                        PersistedEvidenceClockState persistedState,
                                        PersistedEvidenceClockAnchor persistedAnchor,
                                        AuthenticatedTimeSample authenticatedNetworkTime,
                                        long systemWallEpochMs, long elapsedRealtimeMs,
                                        long releaseBuildEpochMs,
                                        Long authenticatedNetworkNowEpochMs,
                                        Long monotonicFloorEpochMs,
                                        long evaluatedAtEpochMs,
                                        EvidenceClockTrust trust) {
        this.currentBootSession = currentBootSession;
        this.persistedState = persistedState;
        this.persistedAnchor = persistedAnchor;
        this.authenticatedNetworkTime = authenticatedNetworkTime;
        this.systemWallEpochMs = systemWallEpochMs;
        this.elapsedRealtimeMs = elapsedRealtimeMs;
        this.releaseBuildEpochMs = releaseBuildEpochMs;
        this.authenticatedNetworkNowEpochMs = authenticatedNetworkNowEpochMs;
        this.monotonicFloorEpochMs = monotonicFloorEpochMs;
        this.evaluatedAtEpochMs = evaluatedAtEpochMs;
        this.trust = trust;
    }

    public BootSessionIdentity currentBootSession() { return currentBootSession; }
    public PersistedEvidenceClockState persistedState() { return persistedState; }
    public PersistedEvidenceClockAnchor persistedAnchor() { return persistedAnchor; }
    public AuthenticatedTimeSample authenticatedNetworkTime() {
        return authenticatedNetworkTime;
    }
    public long systemWallEpochMs() { return systemWallEpochMs; }
    public long elapsedRealtimeMs() { return elapsedRealtimeMs; }
    public long releaseBuildEpochMs() { return releaseBuildEpochMs; }
    public Long authenticatedNetworkNowEpochMs() { return authenticatedNetworkNowEpochMs; }
    public Long monotonicFloorEpochMs() { return monotonicFloorEpochMs; }
    public long evaluatedAtEpochMs() { return evaluatedAtEpochMs; }
    public EvidenceClockTrust trust() { return trust; }
}
