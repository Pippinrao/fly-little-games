package com.flynes.emu.video.quality;

import static org.junit.Assert.assertEquals;

import com.flynes.emu.settings.AspectMode;

import java.util.ArrayList;
import java.util.List;

import org.junit.Test;

public final class EvidenceValidityWatchdogTest {
    @Test public void monotonicDeadlineCanOnlyMoveEarlier() {
        RecordingSink sink = new RecordingSink(true);
        EvidenceValidityWatchdog watchdog = new EvidenceValidityWatchdog(sink);
        CertifiedVideoConfiguration certificate = certificate(2_000L);

        assertEquals(EvidenceInvalidReason.NONE,
                watchdog.observe("profile", certificate, trustedClock(1_000L, 1_000L)));
        assertEquals(Long.valueOf(2_000L), watchdog.scheduledDeadlineElapsedMs(
                certificate.certificateIdentitySha256("profile")));

        assertEquals(EvidenceInvalidReason.NONE,
                watchdog.observe("profile", certificate, trustedClock(1_500L, 1_200L)));
        assertEquals(Long.valueOf(1_700L), watchdog.scheduledDeadlineElapsedMs(
                certificate.certificateIdentitySha256("profile")));

        assertEquals(EvidenceInvalidReason.NONE,
                watchdog.observe("profile", certificate, trustedClock(1_100L, 1_300L)));
        assertEquals(Long.valueOf(1_700L), watchdog.scheduledDeadlineElapsedMs(
                certificate.certificateIdentitySha256("profile")));
    }

    @Test public void tombstoneMustPersistBeforeExpiryIsPublished() {
        CertifiedVideoConfiguration certificate = certificate(2_000L);
        RecordingSink successfulSink = new RecordingSink(true);
        EvidenceValidityWatchdog successful = new EvidenceValidityWatchdog(successfulSink);
        assertEquals(EvidenceInvalidReason.EVIDENCE_EXPIRED,
                successful.observe("profile", certificate, trustedClock(2_000L, 1_000L)));
        assertEquals(1, successfulSink.tombstones.size());

        EvidenceValidityWatchdog failed = new EvidenceValidityWatchdog(
                new RecordingSink(false));
        assertEquals(EvidenceInvalidReason.CLOCK_STATE_CORRUPT,
                failed.observe("profile", certificate, trustedClock(2_000L, 1_000L)));
    }

    @Test public void untrustedClockCannotScheduleOrValidateCertificate() {
        EvidenceValidityWatchdog watchdog = new EvidenceValidityWatchdog(
                new RecordingSink(true));
        TrustedEvidenceClockSnapshot untrusted = new TrustedEvidenceClockSnapshot(null,
                PersistedEvidenceClockState.NONE, null, null, 1_000L, 1_000L,
                1_000L, null, null, 1_000L, EvidenceClockTrust.TIME_UNTRUSTED);
        assertEquals(EvidenceInvalidReason.TIME_UNTRUSTED,
                watchdog.observe("profile", certificate(2_000L), untrusted));
    }

    private static CertifiedVideoConfiguration certificate(long validUntil) {
        VideoConfigurationKey key = new VideoConfigurationKey(SourceTiming.NTSC_60_0988,
                2340, 1080, 1, 60_000, TemporalMode.NATIVE, SpatialMode.MMPX,
                PostEffect.NONE, AspectMode.FOUR_BY_THREE);
        return new CertifiedVideoConfiguration("configuration", key,
                EvidenceLevel.COMPATIBILITY_AND_POWER, "build", "algorithm",
                1_000L, validUntil,
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    }

    private static TrustedEvidenceClockSnapshot trustedClock(long evaluatedAt, long elapsed) {
        BootSessionIdentity boot = new BootSessionIdentity(7, null, "boot");
        return new TrustedEvidenceClockSnapshot(boot, PersistedEvidenceClockState.COMPLETE,
                null, null, evaluatedAt, elapsed, 1L, evaluatedAt, evaluatedAt,
                evaluatedAt, EvidenceClockTrust.TRUSTED);
    }

    private static final class RecordingSink
            implements EvidenceValidityWatchdog.TombstoneSink {
        final boolean succeeds;
        final List<EvidenceExpiryTombstone> tombstones = new ArrayList<>();

        RecordingSink(boolean succeeds) { this.succeeds = succeeds; }

        @Override public boolean persist(EvidenceExpiryTombstone tombstone) {
            if (succeeds) tombstones.add(tombstone);
            return succeeds;
        }
    }
}
