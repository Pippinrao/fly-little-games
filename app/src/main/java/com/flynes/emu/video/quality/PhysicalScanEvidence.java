package com.flynes.emu.video.quality;

import java.util.Objects;

public final class PhysicalScanEvidence {
    private final String profileId;
    private final String configurationId;
    private final VideoConfigurationKey configuration;
    private final PhysicalScanEvidenceState state;
    private final EvidenceInvalidReason invalidReason;
    private final long evaluatedAtEpochMs;
    private final EvidenceClockTrust clockTrust;

    public PhysicalScanEvidence(String profileId, String configurationId,
                                VideoConfigurationKey configuration,
                                PhysicalScanEvidenceState state,
                                EvidenceInvalidReason invalidReason,
                                long evaluatedAtEpochMs,
                                EvidenceClockTrust clockTrust) {
        this.profileId = Objects.requireNonNull(profileId, "profileId");
        this.configurationId = Objects.requireNonNull(configurationId, "configurationId");
        this.configuration = Objects.requireNonNull(configuration, "configuration");
        this.state = Objects.requireNonNull(state, "state");
        this.invalidReason = Objects.requireNonNull(invalidReason, "invalidReason");
        this.evaluatedAtEpochMs = evaluatedAtEpochMs;
        this.clockTrust = Objects.requireNonNull(clockTrust, "clockTrust");
        boolean verified = state == PhysicalScanEvidenceState.VERIFIED;
        boolean validVerifiedState = invalidReason == EvidenceInvalidReason.NONE
                && clockTrust == EvidenceClockTrust.TRUSTED;
        if ((verified && !validVerifiedState)
                || (!verified && invalidReason == EvidenceInvalidReason.NONE)) {
            throw new IllegalArgumentException("inconsistent physical evidence state");
        }
    }

    public String profileId() { return profileId; }
    public String configurationId() { return configurationId; }
    public VideoConfigurationKey configuration() { return configuration; }
    public PhysicalScanEvidenceState state() { return state; }
    public EvidenceInvalidReason invalidReason() { return invalidReason; }
    public long evaluatedAtEpochMs() { return evaluatedAtEpochMs; }
    public EvidenceClockTrust clockTrust() { return clockTrust; }
}
