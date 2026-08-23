package com.flynes.emu.catalog.scan;

/** Shared scanner/loader bounds. Every bound uses strict {@code actual > limit} rejection. */
public record ScanLimits(
        long maxPackageBytes,
        long maxPayloadBytes,
        int maxZipEntries,
        long maxCumulativeInflatedBytes,
        int maxNameBytes,
        int maxCompressionRatio,
        long ratioGuardThresholdBytes) {

    public static final long MIB = 1024L * 1024L;

    public ScanLimits {
        if (maxPackageBytes <= 0
                || maxPayloadBytes <= 0
                || maxZipEntries <= 0
                || maxCumulativeInflatedBytes <= 0
                || maxNameBytes <= 0
                || maxNameBytes > 0xFFFF
                || maxCompressionRatio <= 0
                || ratioGuardThresholdBytes < 0) {
            throw new IllegalArgumentException("scan limits must be positive and ZIP-compatible");
        }
    }

    public static ScanLimits defaults() {
        return new ScanLimits(
                32L * MIB,
                8L * MIB,
                2048,
                32L * MIB,
                1024,
                200,
                1L * MIB);
    }
}
