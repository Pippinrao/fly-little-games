package com.flynes.emu.video.quality;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

public final class PersistedEvidenceClockAnchor {
    private final String bootSessionIdentitySha256;
    private final AuthenticatedTimeSample bootstrapAuthenticatedTime;
    private final long anchorEvaluatedAtEpochMs;
    private final long anchorSystemWallEpochMs;
    private final long anchorElapsedRealtimeMs;
    private final long persistedLastSeenValidatedEpochMs;
    private final List<EvidenceExpiryTombstone> expiryTombstones;
    private final String authenticatedPayloadMac;

    public PersistedEvidenceClockAnchor(String bootSessionIdentitySha256,
                                        AuthenticatedTimeSample bootstrapAuthenticatedTime,
                                        long anchorEvaluatedAtEpochMs,
                                        long anchorSystemWallEpochMs,
                                        long anchorElapsedRealtimeMs,
                                        long persistedLastSeenValidatedEpochMs,
                                        List<EvidenceExpiryTombstone> expiryTombstones,
                                        String authenticatedPayloadMac) {
        this.bootSessionIdentitySha256 = Objects.requireNonNull(
                bootSessionIdentitySha256, "bootSessionIdentitySha256");
        this.bootstrapAuthenticatedTime = Objects.requireNonNull(
                bootstrapAuthenticatedTime, "bootstrapAuthenticatedTime");
        this.anchorEvaluatedAtEpochMs = anchorEvaluatedAtEpochMs;
        this.anchorSystemWallEpochMs = anchorSystemWallEpochMs;
        this.anchorElapsedRealtimeMs = anchorElapsedRealtimeMs;
        this.persistedLastSeenValidatedEpochMs = persistedLastSeenValidatedEpochMs;
        this.expiryTombstones = Collections.unmodifiableList(new ArrayList<>(
                Objects.requireNonNull(expiryTombstones, "expiryTombstones")));
        this.authenticatedPayloadMac = Objects.requireNonNull(
                authenticatedPayloadMac, "authenticatedPayloadMac");
    }

    public String bootSessionIdentitySha256() { return bootSessionIdentitySha256; }
    public AuthenticatedTimeSample bootstrapAuthenticatedTime() {
        return bootstrapAuthenticatedTime;
    }
    public long anchorEvaluatedAtEpochMs() { return anchorEvaluatedAtEpochMs; }
    public long anchorSystemWallEpochMs() { return anchorSystemWallEpochMs; }
    public long anchorElapsedRealtimeMs() { return anchorElapsedRealtimeMs; }
    public long persistedLastSeenValidatedEpochMs() {
        return persistedLastSeenValidatedEpochMs;
    }
    public List<EvidenceExpiryTombstone> expiryTombstones() { return expiryTombstones; }
    public String authenticatedPayloadMac() { return authenticatedPayloadMac; }
}
