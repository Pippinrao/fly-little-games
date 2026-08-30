package com.flynes.emu.video.quality;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;

/** Owns non-extendable per-certificate deadlines for the current boot. */
public final class EvidenceValidityWatchdog {
    public interface TombstoneSink {
        /** Must atomically retain all existing tombstones and add this exact identity. */
        boolean persist(EvidenceExpiryTombstone tombstone);
    }

    private final TombstoneSink tombstoneSink;
    private final Map<String, Long> deadlinesByCertificateIdentity = new LinkedHashMap<>();

    public EvidenceValidityWatchdog(TombstoneSink tombstoneSink) {
        this.tombstoneSink = Objects.requireNonNull(tombstoneSink, "tombstoneSink");
    }

    public synchronized EvidenceInvalidReason observe(String profileId,
                                                      CertifiedVideoConfiguration certificate,
                                                      TrustedEvidenceClockSnapshot clock) {
        Objects.requireNonNull(profileId, "profileId");
        Objects.requireNonNull(certificate, "certificate");
        Objects.requireNonNull(clock, "clock");
        if (clock.trust() != EvidenceClockTrust.TRUSTED) {
            return EvidenceInvalidReason.TIME_UNTRUSTED;
        }

        EvidenceInvalidReason validity = EvidenceValidityPolicy.evaluate(
                certificate, clock.evaluatedAtEpochMs());
        if (validity == EvidenceInvalidReason.INVALID_VALIDITY_RANGE
                || validity == EvidenceInvalidReason.VALIDITY_TOO_LONG
                || validity == EvidenceInvalidReason.FUTURE_ISSUED) {
            return validity;
        }

        String identity = certificate.certificateIdentitySha256(profileId);
        long candidateDeadline;
        try {
            long remaining = Math.subtractExact(certificate.validUntilEpochMs(),
                    clock.evaluatedAtEpochMs());
            candidateDeadline = Math.addExact(clock.elapsedRealtimeMs(),
                    Math.max(0L, remaining));
        } catch (ArithmeticException invalidClockMath) {
            return EvidenceInvalidReason.TIME_UNTRUSTED;
        }
        Long previous = deadlinesByCertificateIdentity.get(identity);
        long scheduled = previous == null ? candidateDeadline
                : Math.min(previous, candidateDeadline);
        deadlinesByCertificateIdentity.put(identity, scheduled);

        boolean expired = validity == EvidenceInvalidReason.EVIDENCE_EXPIRED
                || clock.elapsedRealtimeMs() >= scheduled;
        if (!expired) return EvidenceInvalidReason.NONE;

        EvidenceExpiryTombstone tombstone = new EvidenceExpiryTombstone(identity,
                profileId, certificate.configurationId(), certificate.evidenceManifestSha256(),
                certificate.validUntilEpochMs(), clock.evaluatedAtEpochMs());
        return tombstoneSink.persist(tombstone) ? EvidenceInvalidReason.EVIDENCE_EXPIRED
                : EvidenceInvalidReason.CLOCK_STATE_CORRUPT;
    }

    public synchronized Long scheduledDeadlineElapsedMs(String certificateIdentitySha256) {
        return deadlinesByCertificateIdentity.get(certificateIdentitySha256);
    }
}
