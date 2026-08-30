package com.flynes.emu.video.quality;

import java.util.Objects;

public final class BootSessionIdentity {
    private final Integer androidBootCount;
    private final String kernelBootIdSha256;
    private final String canonicalIdentitySha256;

    public BootSessionIdentity(Integer androidBootCount, String kernelBootIdSha256,
                               String canonicalIdentitySha256) {
        if (androidBootCount == null && isBlank(kernelBootIdSha256)) {
            throw new IllegalArgumentException("At least one platform boot identity is required");
        }
        this.androidBootCount = androidBootCount;
        this.kernelBootIdSha256 = kernelBootIdSha256;
        this.canonicalIdentitySha256 = Objects.requireNonNull(
                canonicalIdentitySha256, "canonicalIdentitySha256");
        if (isBlank(canonicalIdentitySha256)) {
            throw new IllegalArgumentException("canonicalIdentitySha256 is blank");
        }
    }

    private static boolean isBlank(String value) {
        return value == null || value.trim().isEmpty();
    }

    public Integer androidBootCount() { return androidBootCount; }
    public String kernelBootIdSha256() { return kernelBootIdSha256; }
    public String canonicalIdentitySha256() { return canonicalIdentitySha256; }
}
