package com.flynes.emu.video.quality;

import java.util.Objects;

public final class CertifiedVideoConfiguration {
    private final String configurationId;
    private final VideoConfigurationKey key;
    private final EvidenceLevel evidenceLevel;
    private final String buildImplementationHash;
    private final String algorithmImplementationHash;
    private final long certifiedAtEpochMs;
    private final long validUntilEpochMs;
    private final String evidenceManifestSha256;

    public CertifiedVideoConfiguration(String configurationId,
                                       VideoConfigurationKey key,
                                       EvidenceLevel evidenceLevel,
                                       String buildImplementationHash,
                                       String algorithmImplementationHash,
                                       long certifiedAtEpochMs,
                                       long validUntilEpochMs,
                                       String evidenceManifestSha256) {
        this.configurationId = Objects.requireNonNull(configurationId, "configurationId");
        this.key = Objects.requireNonNull(key, "key");
        this.evidenceLevel = Objects.requireNonNull(evidenceLevel, "evidenceLevel");
        this.buildImplementationHash = Objects.requireNonNull(
                buildImplementationHash, "buildImplementationHash");
        this.algorithmImplementationHash = Objects.requireNonNull(
                algorithmImplementationHash, "algorithmImplementationHash");
        this.certifiedAtEpochMs = certifiedAtEpochMs;
        this.validUntilEpochMs = validUntilEpochMs;
        this.evidenceManifestSha256 = Objects.requireNonNull(
                evidenceManifestSha256, "evidenceManifestSha256");
    }

    public String configurationId() { return configurationId; }
    public VideoConfigurationKey key() { return key; }
    public EvidenceLevel evidenceLevel() { return evidenceLevel; }
    public String buildImplementationHash() { return buildImplementationHash; }
    public String algorithmImplementationHash() { return algorithmImplementationHash; }
    public long certifiedAtEpochMs() { return certifiedAtEpochMs; }
    public long validUntilEpochMs() { return validUntilEpochMs; }
    public String evidenceManifestSha256() { return evidenceManifestSha256; }
}
