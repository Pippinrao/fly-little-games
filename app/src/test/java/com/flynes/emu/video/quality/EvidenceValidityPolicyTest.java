package com.flynes.emu.video.quality;

import static org.junit.Assert.assertEquals;

import com.flynes.emu.settings.AspectMode;

import org.junit.Test;

public final class EvidenceValidityPolicyTest {
    private static final long DAY_MS = 86_400_000L;

    @Test public void deviceLabUsesExactHalfOpenExpiryAndOneHundredEightyDayMaximum() {
        CertifiedVideoConfiguration certificate = certificate(EvidenceLevel.DEVICE_LAB,
                1_000L, 1_000L + 180L * DAY_MS);

        assertEquals(EvidenceInvalidReason.NONE,
                EvidenceValidityPolicy.evaluate(certificate,
                        certificate.validUntilEpochMs() - 1L));
        assertEquals(EvidenceInvalidReason.EVIDENCE_EXPIRED,
                EvidenceValidityPolicy.evaluate(certificate,
                        certificate.validUntilEpochMs()));
        assertEquals(EvidenceInvalidReason.VALIDITY_TOO_LONG,
                EvidenceValidityPolicy.evaluate(certificate(EvidenceLevel.DEVICE_LAB,
                        1_000L, 1_001L + 180L * DAY_MS), 2_000L));
    }

    @Test public void compatibilityEvidenceAllowsAtMostThreeHundredSixtyFiveDays() {
        CertifiedVideoConfiguration valid = certificate(EvidenceLevel.COMPATIBILITY_AND_POWER,
                1_000L, 1_000L + 365L * DAY_MS);
        assertEquals(EvidenceInvalidReason.NONE,
                EvidenceValidityPolicy.evaluate(valid, 2_000L));
        assertEquals(EvidenceInvalidReason.VALIDITY_TOO_LONG,
                EvidenceValidityPolicy.evaluate(certificate(
                        EvidenceLevel.COMPATIBILITY_AND_POWER, 1_000L,
                        1_001L + 365L * DAY_MS), 2_000L));
    }

    @Test public void invalidRangesAndFutureIssuanceFailClosedBeforeExpiry() {
        assertEquals(EvidenceInvalidReason.INVALID_VALIDITY_RANGE,
                EvidenceValidityPolicy.evaluate(certificate(EvidenceLevel.DEVICE_LAB,
                        0L, 1_000L), 1L));
        assertEquals(EvidenceInvalidReason.INVALID_VALIDITY_RANGE,
                EvidenceValidityPolicy.evaluate(certificate(EvidenceLevel.DEVICE_LAB,
                        -1L, 1_000L), 1L));
        assertEquals(EvidenceInvalidReason.INVALID_VALIDITY_RANGE,
                EvidenceValidityPolicy.evaluate(certificate(EvidenceLevel.DEVICE_LAB,
                        1_000L, 1_000L), 1_000L));
        assertEquals(EvidenceInvalidReason.INVALID_VALIDITY_RANGE,
                EvidenceValidityPolicy.evaluate(certificate(EvidenceLevel.DEVICE_LAB,
                        2_000L, 1_000L), 1_500L));
        assertEquals(EvidenceInvalidReason.FUTURE_ISSUED,
                EvidenceValidityPolicy.evaluate(certificate(EvidenceLevel.DEVICE_LAB,
                        2_000L, 3_000L), 1_999L));
    }

    private static CertifiedVideoConfiguration certificate(EvidenceLevel level,
                                                            long certifiedAt,
                                                            long validUntil) {
        VideoConfigurationKey key = new VideoConfigurationKey(SourceTiming.NTSC_60_0988,
                2340, 1080, 1, 60_000, TemporalMode.NATIVE,
                SpatialMode.MMPX, PostEffect.NONE, AspectMode.FOUR_BY_THREE);
        return new CertifiedVideoConfiguration("configuration", key, level,
                "build-hash", "algorithm-hash", certifiedAt, validUntil,
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    }
}
