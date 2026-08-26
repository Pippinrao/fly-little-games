package com.flynes.emu.video.quality;

import java.util.Objects;

public final class AuthenticatedReleaseTime {
    private final AuthenticatedReleaseTimeRole role;
    private final AuthenticatedTimeSource source;
    private final long evaluatedAtEpochMs;
    private final String requestNonceSha256;
    private final String authorityIdentitySha256;
    private final String boundCandidateRecordSha256;
    private final String boundCandidateManifestSha256;
    private final String boundReleaseRecordSha256;
    private final String boundReleaseGateSetSha256;
    private final String proofSha256;

    public AuthenticatedReleaseTime(AuthenticatedReleaseTimeRole role,
                                    AuthenticatedTimeSource source,
                                    long evaluatedAtEpochMs,
                                    String requestNonceSha256,
                                    String authorityIdentitySha256,
                                    String boundCandidateRecordSha256,
                                    String boundCandidateManifestSha256,
                                    String boundReleaseRecordSha256,
                                    String boundReleaseGateSetSha256,
                                    String proofSha256) {
        this.role = Objects.requireNonNull(role, "role");
        this.source = Objects.requireNonNull(source, "source");
        if (source == AuthenticatedTimeSource.ANDROID_NETWORK_TIME) {
            throw new IllegalArgumentException("Android network time is not release evidence");
        }
        this.evaluatedAtEpochMs = evaluatedAtEpochMs;
        this.requestNonceSha256 = Objects.requireNonNull(requestNonceSha256,
                "requestNonceSha256");
        this.authorityIdentitySha256 = Objects.requireNonNull(authorityIdentitySha256,
                "authorityIdentitySha256");
        this.boundCandidateRecordSha256 = boundCandidateRecordSha256;
        this.boundCandidateManifestSha256 = boundCandidateManifestSha256;
        this.boundReleaseRecordSha256 = boundReleaseRecordSha256;
        this.boundReleaseGateSetSha256 = boundReleaseGateSetSha256;
        this.proofSha256 = Objects.requireNonNull(proofSha256, "proofSha256");
    }

    public AuthenticatedReleaseTimeRole role() { return role; }
    public AuthenticatedTimeSource source() { return source; }
    public long evaluatedAtEpochMs() { return evaluatedAtEpochMs; }
    public String requestNonceSha256() { return requestNonceSha256; }
    public String authorityIdentitySha256() { return authorityIdentitySha256; }
    public String boundCandidateRecordSha256() { return boundCandidateRecordSha256; }
    public String boundCandidateManifestSha256() { return boundCandidateManifestSha256; }
    public String boundReleaseRecordSha256() { return boundReleaseRecordSha256; }
    public String boundReleaseGateSetSha256() { return boundReleaseGateSetSha256; }
    public String proofSha256() { return proofSha256; }
}
