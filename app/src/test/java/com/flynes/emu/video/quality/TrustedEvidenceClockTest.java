package com.flynes.emu.video.quality;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import java.util.Collections;

import org.junit.Test;

public final class TrustedEvidenceClockTest {
    @Test public void sameBootAdvancesFromAuthenticatedAnchorDespiteSmallWallRollback() {
        BootSessionIdentity boot = boot("boot-a");
        AuthenticatedTimeSample bootstrap = sample(boot, 1_000_000L, 1_000L);
        PersistedEvidenceClockAnchor anchor = anchor(boot, bootstrap,
                1_000_000L, 1_000_000L, 1_000L, 1_000_000L);

        TrustedEvidenceClockSnapshot snapshot = TrustedEvidenceClock.evaluate(boot,
                PersistedEvidenceClockState.COMPLETE, anchor, null,
                999_000L, 4_000L, 900_000L);

        assertEquals(EvidenceClockTrust.TRUSTED, snapshot.trust());
        assertEquals(Long.valueOf(1_003_000L), snapshot.authenticatedNetworkNowEpochMs());
        assertEquals(Long.valueOf(1_003_000L), snapshot.monotonicFloorEpochMs());
        assertEquals(1_003_000L, snapshot.evaluatedAtEpochMs());
    }

    @Test public void lastValidatedTimeRemainsAOneWayFloor() {
        BootSessionIdentity boot = boot("boot-a");
        PersistedEvidenceClockAnchor anchor = anchor(boot,
                sample(boot, 1_000_000L, 1_000L),
                1_000_000L, 1_000_000L, 1_000L, 2_000_000L);

        TrustedEvidenceClockSnapshot snapshot = TrustedEvidenceClock.evaluate(boot,
                PersistedEvidenceClockState.COMPLETE, anchor, null,
                1_000_100L, 1_100L, 900_000L);

        assertEquals(EvidenceClockTrust.TRUSTED, snapshot.trust());
        assertEquals(2_000_000L, snapshot.evaluatedAtEpochMs());
    }

    @Test public void missingAnchorDifferentBootAndElapsedRollbackFailClosed() {
        BootSessionIdentity bootA = boot("boot-a");
        BootSessionIdentity bootB = boot("boot-b");
        PersistedEvidenceClockAnchor anchor = anchor(bootA,
                sample(bootA, 1_000_000L, 1_000L),
                1_000_000L, 1_000_000L, 1_000L, 1_000_000L);

        TrustedEvidenceClockSnapshot missing = TrustedEvidenceClock.evaluate(bootA,
                PersistedEvidenceClockState.NONE, null, null,
                1_000_000L, 1_000L, 900_000L);
        assertEquals(EvidenceClockTrust.TIME_UNTRUSTED, missing.trust());
        assertNull(missing.monotonicFloorEpochMs());

        assertEquals(EvidenceClockTrust.TIME_UNTRUSTED,
                TrustedEvidenceClock.evaluate(bootB, PersistedEvidenceClockState.COMPLETE,
                        anchor, null, 1_001_000L, 2_000L, 900_000L).trust());
        assertEquals(EvidenceClockTrust.TIME_UNTRUSTED,
                TrustedEvidenceClock.evaluate(bootA, PersistedEvidenceClockState.COMPLETE,
                        anchor, null, 1_000_000L, 999L, 900_000L).trust());
    }

    @Test public void clockDivergenceBeyondTwentyFourHoursAndOverflowAreUntrusted() {
        BootSessionIdentity boot = boot("boot-a");
        PersistedEvidenceClockAnchor anchor = anchor(boot,
                sample(boot, 1_000_000L, 1_000L),
                1_000_000L, 1_000_000L, 1_000L, 1_000_000L);

        long beyondTolerance = 86_400_001L;
        assertEquals(EvidenceClockTrust.TIME_UNTRUSTED,
                TrustedEvidenceClock.evaluate(boot, PersistedEvidenceClockState.COMPLETE,
                        anchor, null, 1_000_000L + beyondTolerance,
                        1_000L, 900_000L).trust());

        PersistedEvidenceClockAnchor overflowing = anchor(boot,
                sample(boot, Long.MAX_VALUE - 1L, 1L),
                Long.MAX_VALUE - 1L, Long.MAX_VALUE - 1L, 1L,
                Long.MAX_VALUE - 1L);
        assertEquals(EvidenceClockTrust.TIME_UNTRUSTED,
                TrustedEvidenceClock.evaluate(boot, PersistedEvidenceClockState.COMPLETE,
                        overflowing, null, Long.MAX_VALUE - 1L,
                        10L, Long.MAX_VALUE - 1L).trust());
    }

    private static BootSessionIdentity boot(String canonical) {
        return new BootSessionIdentity(7, null, canonical);
    }

    private static AuthenticatedTimeSample sample(BootSessionIdentity boot,
                                                   long epoch,
                                                   long elapsed) {
        return new AuthenticatedTimeSample(AuthenticatedTimeSource.ANDROID_NETWORK_TIME,
                epoch, elapsed, boot.canonicalIdentitySha256(), "provenance");
    }

    private static PersistedEvidenceClockAnchor anchor(BootSessionIdentity boot,
                                                        AuthenticatedTimeSample sample,
                                                        long evaluated,
                                                        long wall,
                                                        long elapsed,
                                                        long lastSeen) {
        return new PersistedEvidenceClockAnchor(boot.canonicalIdentitySha256(), sample,
                evaluated, wall, elapsed, lastSeen, Collections.emptyList(), "mac");
    }
}
