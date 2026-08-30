package com.flynes.emu.video.quality;

import java.util.Objects;

public final class TrustedEvidenceClock {
    public static final long EVIDENCE_CLOCK_ANOMALY_TOLERANCE_MS = 86_400_000L;

    private TrustedEvidenceClock() {}

    public static TrustedEvidenceClockSnapshot evaluate(
            BootSessionIdentity currentBoot,
            PersistedEvidenceClockState persistedState,
            PersistedEvidenceClockAnchor anchor,
            AuthenticatedTimeSample currentNetworkTime,
            long systemWallEpochMs,
            long elapsedRealtimeMs,
            long releaseBuildEpochMs) {
        Objects.requireNonNull(persistedState, "persistedState");
        long fallbackEpoch = maximum(systemWallEpochMs, releaseBuildEpochMs,
                anchor == null ? Long.MIN_VALUE : anchor.persistedLastSeenValidatedEpochMs());
        if (!hasCompleteCurrentBootAnchor(currentBoot, persistedState, anchor)) {
            return snapshot(currentBoot, persistedState, anchor, currentNetworkTime,
                    systemWallEpochMs, elapsedRealtimeMs, releaseBuildEpochMs,
                    null, null, fallbackEpoch, EvidenceClockTrust.TIME_UNTRUSTED);
        }

        AuthenticatedTimeSample networkTime = currentNetworkTime == null
                ? anchor.bootstrapAuthenticatedTime() : currentNetworkTime;
        if (!validNetworkTime(networkTime, currentBoot)) {
            return snapshot(currentBoot, persistedState, anchor, networkTime,
                    systemWallEpochMs, elapsedRealtimeMs, releaseBuildEpochMs,
                    null, null, fallbackEpoch, EvidenceClockTrust.TIME_UNTRUSTED);
        }

        try {
            long elapsedDelta = Math.subtractExact(elapsedRealtimeMs,
                    anchor.anchorElapsedRealtimeMs());
            long networkElapsedDelta = Math.subtractExact(elapsedRealtimeMs,
                    networkTime.sampledAtElapsedRealtimeMs());
            if (elapsedDelta < 0L || networkElapsedDelta < 0L) {
                throw new ArithmeticException("elapsedRealtime moved backwards");
            }
            long monotonicFloor = Math.addExact(anchor.anchorEvaluatedAtEpochMs(), elapsedDelta);
            long networkNow = Math.addExact(networkTime.sampledEpochMs(), networkElapsedDelta);
            long wallDelta = Math.subtractExact(systemWallEpochMs,
                    anchor.anchorSystemWallEpochMs());
            if (differenceExceeds(wallDelta, elapsedDelta,
                    EVIDENCE_CLOCK_ANOMALY_TOLERANCE_MS)
                    || differenceExceeds(systemWallEpochMs, monotonicFloor,
                    EVIDENCE_CLOCK_ANOMALY_TOLERANCE_MS)
                    || differenceExceeds(networkNow, monotonicFloor,
                    EVIDENCE_CLOCK_ANOMALY_TOLERANCE_MS)) {
                return snapshot(currentBoot, persistedState, anchor, networkTime,
                        systemWallEpochMs, elapsedRealtimeMs, releaseBuildEpochMs,
                        networkNow, monotonicFloor,
                        maximum(fallbackEpoch, networkNow, monotonicFloor),
                        EvidenceClockTrust.TIME_UNTRUSTED);
            }
            long evaluatedAt = maximum(systemWallEpochMs, releaseBuildEpochMs,
                    anchor.persistedLastSeenValidatedEpochMs(), monotonicFloor, networkNow);
            return snapshot(currentBoot, persistedState, anchor, networkTime,
                    systemWallEpochMs, elapsedRealtimeMs, releaseBuildEpochMs,
                    networkNow, monotonicFloor, evaluatedAt, EvidenceClockTrust.TRUSTED);
        } catch (ArithmeticException invalidClockMath) {
            return snapshot(currentBoot, persistedState, anchor, networkTime,
                    systemWallEpochMs, elapsedRealtimeMs, releaseBuildEpochMs,
                    null, null, fallbackEpoch, EvidenceClockTrust.TIME_UNTRUSTED);
        }
    }

    private static boolean hasCompleteCurrentBootAnchor(
            BootSessionIdentity currentBoot,
            PersistedEvidenceClockState state,
            PersistedEvidenceClockAnchor anchor) {
        return currentBoot != null && state == PersistedEvidenceClockState.COMPLETE
                && anchor != null
                && currentBoot.canonicalIdentitySha256().equals(
                anchor.bootSessionIdentitySha256());
    }

    private static boolean validNetworkTime(AuthenticatedTimeSample sample,
                                            BootSessionIdentity currentBoot) {
        return sample != null
                && sample.source() == AuthenticatedTimeSource.ANDROID_NETWORK_TIME
                && sample.sampledEpochMs() > 0L
                && sample.sampledAtElapsedRealtimeMs() >= 0L
                && !sample.provenanceSha256().trim().isEmpty()
                && currentBoot.canonicalIdentitySha256().equals(
                sample.bootSessionIdentitySha256());
    }

    private static boolean differenceExceeds(long left, long right, long tolerance) {
        try {
            long difference = Math.subtractExact(left, right);
            return difference > tolerance || difference < -tolerance;
        } catch (ArithmeticException overflow) {
            return true;
        }
    }

    private static long maximum(long... values) {
        long maximum = Long.MIN_VALUE;
        for (long value : values) maximum = Math.max(maximum, value);
        return maximum;
    }

    private static TrustedEvidenceClockSnapshot snapshot(
            BootSessionIdentity currentBoot,
            PersistedEvidenceClockState persistedState,
            PersistedEvidenceClockAnchor anchor,
            AuthenticatedTimeSample networkTime,
            long systemWallEpochMs,
            long elapsedRealtimeMs,
            long releaseBuildEpochMs,
            Long networkNow,
            Long monotonicFloor,
            long evaluatedAt,
            EvidenceClockTrust trust) {
        return new TrustedEvidenceClockSnapshot(currentBoot, persistedState, anchor,
                networkTime, systemWallEpochMs, elapsedRealtimeMs, releaseBuildEpochMs,
                networkNow, monotonicFloor, evaluatedAt, trust);
    }
}
