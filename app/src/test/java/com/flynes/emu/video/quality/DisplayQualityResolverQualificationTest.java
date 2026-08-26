package com.flynes.emu.video.quality;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.settings.AspectMode;
import com.flynes.emu.video.power.ThermalBand;

import java.util.Arrays;
import java.util.Collections;

import org.junit.Test;

public final class DisplayQualityResolverQualificationTest {
    private static final long NOW_EPOCH_MS = 1_700_000_000_000L;

    @Test public void balancedUsesExactQualifiedMmpxConfiguration() {
        Fixture fixture = fixture(AspectMode.FOUR_BY_THREE, EvidenceLevel.COMPATIBILITY_AND_POWER,
                "mmpx-hash", NOW_EPOCH_MS - 1_000L, NOW_EPOCH_MS + 10_000L);

        EffectiveVideoConfig result = fixture.resolve(AspectMode.FOUR_BY_THREE,
                fixture.profile, fixture.clock);

        assertEquals(SpatialMode.MMPX, result.effectiveSpatial());
        assertEquals("mmpx-qualified", result.resolvedConfigurationId());
        assertEquals(fixture.certificate.key(), result.resolvedConfigurationKey());
        assertTrue(result.fallbacks().isEmpty());
    }

    @Test public void changedAspectAndAlgorithmHashDoNotReuseCertificate() {
        Fixture fixture = fixture(AspectMode.FOUR_BY_THREE, EvidenceLevel.COMPATIBILITY_AND_POWER,
                "mmpx-hash", NOW_EPOCH_MS - 1_000L, NOW_EPOCH_MS + 10_000L);

        assertEquals(SpatialMode.SHARP_BILINEAR,
                fixture.resolve(AspectMode.SQUARE_PIXELS, fixture.profile,
                        fixture.clock).effectiveSpatial());

        DeviceQualityProfile wrongAlgorithm = profile(fixture.certificate,
                "different-build-profile-hash");
        EffectiveVideoConfig result = fixture.resolve(AspectMode.FOUR_BY_THREE,
                wrongAlgorithm, fixture.clock);
        assertEquals(SpatialMode.SHARP_BILINEAR, result.effectiveSpatial());
        assertTrue(result.fallbacks().contains(FallbackReason.CONFIGURATION_UNVERIFIED));
    }

    @Test public void expiredOrUntrustedEvidenceNeverUnlocksAdvancedTuple() {
        Fixture expired = fixture(AspectMode.FOUR_BY_THREE,
                EvidenceLevel.COMPATIBILITY_AND_POWER, "mmpx-hash",
                NOW_EPOCH_MS - 20_000L, NOW_EPOCH_MS);
        EffectiveVideoConfig expiredResult = expired.resolve(AspectMode.FOUR_BY_THREE,
                expired.profile, expired.clock);
        assertEquals(SpatialMode.SHARP_BILINEAR, expiredResult.effectiveSpatial());
        assertTrue(expiredResult.fallbacks().contains(FallbackReason.EVIDENCE_EXPIRED));

        TrustedEvidenceClockSnapshot untrusted = TrustedEvidenceClock.evaluate(
                expired.boot, PersistedEvidenceClockState.NONE, null, null,
                NOW_EPOCH_MS, 2_000L, NOW_EPOCH_MS - 1_000L);
        EffectiveVideoConfig untrustedResult = expired.resolve(AspectMode.FOUR_BY_THREE,
                expired.profile, untrusted);
        assertEquals(SpatialMode.SHARP_BILINEAR, untrustedResult.effectiveSpatial());
        assertTrue(untrustedResult.fallbacks().contains(FallbackReason.TIME_BOOTSTRAP_REQUIRED));
    }

    private static Fixture fixture(AspectMode aspectMode, EvidenceLevel level,
                                   String algorithmHash, long certifiedAt, long validUntil) {
        DisplayModeCapability mode60 = new DisplayModeCapability(1, 2340, 1080, 60_000);
        VideoConfigurationKey key = new VideoConfigurationKey(SourceTiming.NTSC_60_0988,
                2340, 1080, 1, 60_000, TemporalMode.NATIVE, SpatialMode.MMPX,
                PostEffect.NONE, aspectMode);
        CertifiedVideoConfiguration certificate = new CertifiedVideoConfiguration(
                "mmpx-qualified", key, level, "build-profile-hash", algorithmHash,
                certifiedAt, validUntil,
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        BootSessionIdentity boot = new BootSessionIdentity(7, null, "boot-a");
        AuthenticatedTimeSample sample = new AuthenticatedTimeSample(
                AuthenticatedTimeSource.ANDROID_NETWORK_TIME, NOW_EPOCH_MS, 2_000L,
                boot.canonicalIdentitySha256(), "provenance");
        PersistedEvidenceClockAnchor anchor = new PersistedEvidenceClockAnchor(
                boot.canonicalIdentitySha256(), sample, NOW_EPOCH_MS, NOW_EPOCH_MS,
                2_000L, NOW_EPOCH_MS, Collections.emptyList(), "mac");
        TrustedEvidenceClockSnapshot clock = TrustedEvidenceClock.evaluate(boot,
                PersistedEvidenceClockState.COMPLETE, anchor, null,
                NOW_EPOCH_MS, 2_000L, NOW_EPOCH_MS - 1_000L);
        return new Fixture(mode60, certificate, profile(certificate, "build-profile-hash"),
                boot, clock);
    }

    private static DeviceQualityProfile profile(CertifiedVideoConfiguration certificate,
                                                String buildImplementationHash) {
        return new DeviceQualityProfile("target-phone", "Google", "Pixel",
                "build-fingerprint", "vendor", "renderer", "version",
                "driver-fingerprint", buildImplementationHash,
                Collections.singletonList(certificate), AdaptiveQualityPolicy.empty());
    }

    private static final class Fixture {
        final DisplayModeCapability mode60;
        final CertifiedVideoConfiguration certificate;
        final DeviceQualityProfile profile;
        final BootSessionIdentity boot;
        final TrustedEvidenceClockSnapshot clock;

        Fixture(DisplayModeCapability mode60, CertifiedVideoConfiguration certificate,
                DeviceQualityProfile profile, BootSessionIdentity boot,
                TrustedEvidenceClockSnapshot clock) {
            this.mode60 = mode60;
            this.certificate = certificate;
            this.profile = profile;
            this.boot = boot;
            this.clock = clock;
        }

        EffectiveVideoConfig resolve(AspectMode aspectMode, DeviceQualityProfile selectedProfile,
                                     TrustedEvidenceClockSnapshot selectedClock) {
            DisplayCapabilities display = new DisplayCapabilities(2340, 1080,
                    Arrays.asList(mode60,
                            new DisplayModeCapability(2, 2340, 1080, 120_000)), knownGl());
            BuildAlgorithmAvailability build = new BuildAlgorithmAvailability(
                    true, false, false, 1, "mmpx-hash", "", "", 0, "");
            RuntimeConstraints constraints = new RuntimeConstraints(SourceTiming.NTSC_60_0988,
                    new DisplayObservation(1L, PhysicalRefreshPolicy.HZ_60, mode60, mode60,
                            1_500L, 4_000L), false, 80, 35.0f, ThermalBand.NONE,
                    true, Collections.emptySet(), RuntimeTemporalState.IMMEDIATE_NATIVE);
            DeviceIdentity identity = new DeviceIdentity("Google", "Pixel",
                    "build-fingerprint", "vendor", "renderer", "version",
                    "driver-fingerprint");
            return new DisplayQualityResolver().resolve(VideoPreferences.defaults(), aspectMode,
                    display, build, constraints, identity, selectedProfile, selectedClock,
                    2_000L);
        }
    }

    private static GlCapabilities knownGl() {
        return new GlCapabilities(3, 2, "vendor", "renderer", "version",
                Collections.emptySet(), 8192, true, true, true, true,
                true, true, true);
    }
}
