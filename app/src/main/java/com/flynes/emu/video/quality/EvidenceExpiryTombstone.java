package com.flynes.emu.video.quality;

import java.util.Objects;

public final class EvidenceExpiryTombstone {
    private final String certificateIdentitySha256;
    private final String profileId;
    private final String configurationId;
    private final String evidenceManifestSha256;
    private final long validUntilEpochMs;
    private final long firstObservedExpiredAtEpochMs;

    public EvidenceExpiryTombstone(String certificateIdentitySha256, String profileId,
                                   String configurationId, String evidenceManifestSha256,
                                   long validUntilEpochMs,
                                   long firstObservedExpiredAtEpochMs) {
        this.certificateIdentitySha256 = Objects.requireNonNull(
                certificateIdentitySha256, "certificateIdentitySha256");
        this.profileId = Objects.requireNonNull(profileId, "profileId");
        this.configurationId = Objects.requireNonNull(configurationId, "configurationId");
        this.evidenceManifestSha256 = Objects.requireNonNull(
                evidenceManifestSha256, "evidenceManifestSha256");
        this.validUntilEpochMs = validUntilEpochMs;
        this.firstObservedExpiredAtEpochMs = firstObservedExpiredAtEpochMs;
    }

    public String certificateIdentitySha256() { return certificateIdentitySha256; }
    public String profileId() { return profileId; }
    public String configurationId() { return configurationId; }
    public String evidenceManifestSha256() { return evidenceManifestSha256; }
    public long validUntilEpochMs() { return validUntilEpochMs; }
    public long firstObservedExpiredAtEpochMs() { return firstObservedExpiredAtEpochMs; }
}
