package com.flynes.emu.video.quality;

import java.util.Objects;

public final class EvidenceValidityPolicy {
    private static final long DAY_MS = 86_400_000L;
    private static final long DEVICE_LAB_MAX_MS = 180L * DAY_MS;
    private static final long COMPATIBILITY_MAX_MS = 365L * DAY_MS;

    private EvidenceValidityPolicy() {}

    public static EvidenceInvalidReason evaluate(CertifiedVideoConfiguration certificate,
                                                 long evaluatedAtEpochMs) {
        Objects.requireNonNull(certificate, "certificate");
        long certifiedAt = certificate.certifiedAtEpochMs();
        long validUntil = certificate.validUntilEpochMs();
        if (certifiedAt <= 0L || validUntil <= 0L || validUntil <= certifiedAt) {
            return EvidenceInvalidReason.INVALID_VALIDITY_RANGE;
        }

        long maximumDuration = maximumDuration(certificate.evidenceLevel());
        if (certifiedAt > Long.MAX_VALUE - maximumDuration
                || validUntil > certifiedAt + maximumDuration) {
            return EvidenceInvalidReason.VALIDITY_TOO_LONG;
        }
        if (evaluatedAtEpochMs < certifiedAt) {
            return EvidenceInvalidReason.FUTURE_ISSUED;
        }
        if (evaluatedAtEpochMs >= validUntil) {
            return EvidenceInvalidReason.EVIDENCE_EXPIRED;
        }
        return EvidenceInvalidReason.NONE;
    }

    private static long maximumDuration(EvidenceLevel evidenceLevel) {
        switch (Objects.requireNonNull(evidenceLevel, "evidenceLevel")) {
            case DEVICE_LAB:
                return DEVICE_LAB_MAX_MS;
            case COMPATIBILITY_AND_POWER:
                return COMPATIBILITY_MAX_MS;
            default:
                throw new IllegalArgumentException("Unsupported evidence level: " + evidenceLevel);
        }
    }
}
