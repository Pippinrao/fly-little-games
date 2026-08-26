package com.flynes.emu.video.quality;

import java.util.Objects;

public final class AuthenticatedTimeSample {
    private final AuthenticatedTimeSource source;
    private final long sampledEpochMs;
    private final long sampledAtElapsedRealtimeMs;
    private final String bootSessionIdentitySha256;
    private final String provenanceSha256;

    public AuthenticatedTimeSample(AuthenticatedTimeSource source, long sampledEpochMs,
                                   long sampledAtElapsedRealtimeMs,
                                   String bootSessionIdentitySha256,
                                   String provenanceSha256) {
        this.source = Objects.requireNonNull(source, "source");
        this.sampledEpochMs = sampledEpochMs;
        this.sampledAtElapsedRealtimeMs = sampledAtElapsedRealtimeMs;
        this.bootSessionIdentitySha256 = Objects.requireNonNull(
                bootSessionIdentitySha256, "bootSessionIdentitySha256");
        this.provenanceSha256 = Objects.requireNonNull(provenanceSha256, "provenanceSha256");
    }

    public AuthenticatedTimeSource source() { return source; }
    public long sampledEpochMs() { return sampledEpochMs; }
    public long sampledAtElapsedRealtimeMs() { return sampledAtElapsedRealtimeMs; }
    public String bootSessionIdentitySha256() { return bootSessionIdentitySha256; }
    public String provenanceSha256() { return provenanceSha256; }
}
