package com.flynes.emu.settings;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.video.quality.BootSessionIdentity;
import com.flynes.emu.video.quality.CertifiedVideoConfiguration;
import com.flynes.emu.video.quality.EvidenceClockTrust;
import com.flynes.emu.video.quality.EvidenceLevel;
import com.flynes.emu.video.quality.MotionRiskConsent;
import com.flynes.emu.video.quality.PersistedEvidenceClockState;
import com.flynes.emu.video.quality.PostEffect;
import com.flynes.emu.video.quality.SourceTiming;
import com.flynes.emu.video.quality.SpatialMode;
import com.flynes.emu.video.quality.TemporalMode;
import com.flynes.emu.video.quality.TrustedEvidenceClockSnapshot;
import com.flynes.emu.video.quality.VideoConfigurationKey;

import org.junit.Test;

public final class MotionRiskConsentStoreTest {
    @Test public void clickUsesFreshTrustedSnapshotForBothAcceptanceTimes() {
        Fixture fixture = fixture(1_500L, true);
        MotionRiskConsent consent = fixture.store.accept(fixture.dialogIdentity);

        assertEquals(1_500L, consent.acceptedAtEpochMs());
        assertEquals(1_500L, consent.evidenceEvaluatedAtEpochMs());
        assertEquals(consent, fixture.persistence.saved);
    }

    @Test public void dialogCrossingExpiryAndGenerationCasFailureSaveNothing() {
        Fixture expired = fixture(2_000L, true);
        assertNull(expired.store.accept(expired.dialogIdentity));
        assertNull(expired.persistence.saved);

        Fixture casFailed = fixture(1_500L, false);
        assertNull(casFailed.store.accept(casFailed.dialogIdentity));
        assertNull(casFailed.persistence.saved);
    }

    @Test public void everyRequestRechecksIdentityClockAndValidity() {
        Fixture fixture = fixture(1_500L, true);
        MotionRiskConsent consent = fixture.store.accept(fixture.dialogIdentity);
        assertTrue(fixture.store.isValid(consent, fixture.context));

        MotionRiskConsentStore.CurrentContext replaced = new MotionRiskConsentStore.CurrentContext(
                fixture.context.profileGeneration() + 1L, "other-profile",
                fixture.context.certificate(), fixture.context.clock());
        assertFalse(fixture.store.isValid(consent, replaced));

        TrustedEvidenceClockSnapshot untrusted = clock(1_600L,
                EvidenceClockTrust.TIME_UNTRUSTED);
        assertFalse(fixture.store.isValid(consent,
                new MotionRiskConsentStore.CurrentContext(7L, "profile",
                        fixture.context.certificate(), untrusted)));
        assertFalse(fixture.store.isValid(consent,
                new MotionRiskConsentStore.CurrentContext(7L, "profile",
                        fixture.context.certificate(), clock(2_000L,
                        EvidenceClockTrust.TRUSTED))));
    }

    private static Fixture fixture(long clickEpoch, boolean casSucceeds) {
        VideoConfigurationKey key = new VideoConfigurationKey(SourceTiming.NTSC_60_0988,
                2340, 1080, 2, 120_000, TemporalMode.MOTION_INTERPOLATION,
                SpatialMode.SHARP_BILINEAR, PostEffect.NONE, AspectMode.FOUR_BY_THREE);
        CertifiedVideoConfiguration certificate = new CertifiedVideoConfiguration(
                "motion", key, EvidenceLevel.DEVICE_LAB, "build", "algorithm",
                1_000L, 2_000L,
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        MotionRiskConsentStore.CurrentContext context =
                new MotionRiskConsentStore.CurrentContext(7L, "profile", certificate,
                        clock(clickEpoch, EvidenceClockTrust.TRUSTED));
        FakePersistence persistence = new FakePersistence(casSucceeds);
        MotionRiskConsentStore store = new MotionRiskConsentStore(
                ignored -> context, persistence, 3);
        MotionRiskConsentStore.DialogIdentity dialog =
                MotionRiskConsentStore.DialogIdentity.from("profile", certificate, 3);
        return new Fixture(store, persistence, context, dialog);
    }

    private static TrustedEvidenceClockSnapshot clock(long epoch, EvidenceClockTrust trust) {
        BootSessionIdentity boot = new BootSessionIdentity(7, null, "boot");
        return new TrustedEvidenceClockSnapshot(boot, PersistedEvidenceClockState.COMPLETE,
                null, null, epoch, 100L, 1L, epoch, epoch, epoch, trust);
    }

    private static final class FakePersistence
            implements MotionRiskConsentStore.Persistence {
        final boolean succeeds;
        MotionRiskConsent saved;
        FakePersistence(boolean succeeds) { this.succeeds = succeeds; }
        @Override public boolean compareAndSet(long expectedProfileGeneration,
                                               MotionRiskConsent consent) {
            if (succeeds) saved = consent;
            return succeeds;
        }
    }

    private static final class Fixture {
        final MotionRiskConsentStore store;
        final FakePersistence persistence;
        final MotionRiskConsentStore.CurrentContext context;
        final MotionRiskConsentStore.DialogIdentity dialogIdentity;
        Fixture(MotionRiskConsentStore store, FakePersistence persistence,
                MotionRiskConsentStore.CurrentContext context,
                MotionRiskConsentStore.DialogIdentity dialogIdentity) {
            this.store = store;
            this.persistence = persistence;
            this.context = context;
            this.dialogIdentity = dialogIdentity;
        }
    }
}
