package com.flynes.emu.video.quality;

import java.util.Objects;

public final class MotionRiskConsent {
    private final String profileId;
    private final String configurationId;
    private final VideoConfigurationKey configuration;
    private final String evidenceManifestSha256;
    private final long evidenceCertifiedAtEpochMs;
    private final long evidenceValidUntilEpochMs;
    private final long evidenceEvaluatedAtEpochMs;
    private final String buildImplementationHash;
    private final String algorithmImplementationHash;
    private final int riskCopyVersion;
    private final long acceptedAtEpochMs;

    public MotionRiskConsent(String profileId, String configurationId,
                             VideoConfigurationKey configuration,
                             String evidenceManifestSha256,
                             long evidenceCertifiedAtEpochMs,
                             long evidenceValidUntilEpochMs,
                             long evidenceEvaluatedAtEpochMs,
                             String buildImplementationHash,
                             String algorithmImplementationHash,
                             int riskCopyVersion,
                             long acceptedAtEpochMs) {
        this.profileId = Objects.requireNonNull(profileId, "profileId");
        this.configurationId = Objects.requireNonNull(configurationId, "configurationId");
        this.configuration = Objects.requireNonNull(configuration, "configuration");
        this.evidenceManifestSha256 = Objects.requireNonNull(
                evidenceManifestSha256, "evidenceManifestSha256");
        this.evidenceCertifiedAtEpochMs = evidenceCertifiedAtEpochMs;
        this.evidenceValidUntilEpochMs = evidenceValidUntilEpochMs;
        this.evidenceEvaluatedAtEpochMs = evidenceEvaluatedAtEpochMs;
        this.buildImplementationHash = Objects.requireNonNull(
                buildImplementationHash, "buildImplementationHash");
        this.algorithmImplementationHash = Objects.requireNonNull(
                algorithmImplementationHash, "algorithmImplementationHash");
        this.riskCopyVersion = riskCopyVersion;
        this.acceptedAtEpochMs = acceptedAtEpochMs;
    }

    public String profileId() { return profileId; }
    public String configurationId() { return configurationId; }
    public VideoConfigurationKey configuration() { return configuration; }
    public String evidenceManifestSha256() { return evidenceManifestSha256; }
    public long evidenceCertifiedAtEpochMs() { return evidenceCertifiedAtEpochMs; }
    public long evidenceValidUntilEpochMs() { return evidenceValidUntilEpochMs; }
    public long evidenceEvaluatedAtEpochMs() { return evidenceEvaluatedAtEpochMs; }
    public String buildImplementationHash() { return buildImplementationHash; }
    public String algorithmImplementationHash() { return algorithmImplementationHash; }
    public int riskCopyVersion() { return riskCopyVersion; }
    public long acceptedAtEpochMs() { return acceptedAtEpochMs; }
}
