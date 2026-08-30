package com.flynes.emu.video.quality;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.settings.AspectMode;
import com.flynes.emu.video.power.ThermalBand;

import java.util.Arrays;
import java.util.Collections;

import org.junit.Test;

public final class DisplayMotionLeaseTest {
    private static final long EPOCH = 1_700_000_000_000L;
    private static final long NOW = 10_000L;

    @Test public void motionWaitsForThreeSecondsOfFreshSameGenerationOneTwentyHertz() {
        EffectiveVideoConfig waiting = resolve(SourceTiming.NTSC_60_0988,
                observation(1L, NOW - 500L, 2_999L, mode120()), 1L,
                RuntimeTemporalState.IMMEDIATE_NATIVE);
        assertEquals(TemporalMode.NATIVE, waiting.effectiveTemporal());
        assertEquals(RuntimeTemporalState.IMMEDIATE_NATIVE, waiting.runtimeTemporalState());
        assertEquals(0, waiting.videoDelayFrames());

        EffectiveVideoConfig priming = resolve(SourceTiming.NTSC_60_0988,
                observation(1L, NOW - 500L, 3_000L, mode120()), 1L,
                RuntimeTemporalState.IMMEDIATE_NATIVE);
        assertEquals(TemporalMode.MOTION_INTERPOLATION, priming.effectiveTemporal());
        assertEquals(RuntimeTemporalState.PRIMING, priming.runtimeTemporalState());
        assertNull(priming.resolvedConfigurationId());
        assertNull(priming.resolvedConfigurationKey());
    }

    @Test public void activeMotionRequiresContinuousFreshLeaseAndRetainsDelayOnLoss() {
        EffectiveVideoConfig active = resolve(SourceTiming.NTSC_60_0988,
                observation(3L, NOW - 500L, 4_000L, mode120()), 3L,
                RuntimeTemporalState.MOTION_COMPENSATING);
        assertEquals(TemporalMode.MOTION_INTERPOLATION, active.effectiveTemporal());
        assertEquals(RuntimeTemporalState.MOTION_COMPENSATING,
                active.runtimeTemporalState());
        assertEquals(1, active.videoDelayFrames());
        assertEquals(16.7f, active.audioDelayMs(), 0.1f);

        EffectiveVideoConfig stale = resolve(SourceTiming.NTSC_60_0988,
                observation(3L, NOW - 1_500L, 6_000L, mode120()), 3L,
                RuntimeTemporalState.MOTION_COMPENSATING);
        assertEquals(TemporalMode.NATIVE, stale.effectiveTemporal());
        assertEquals(RuntimeTemporalState.BUFFERED_NATIVE_HOLD,
                stale.runtimeTemporalState());
        assertEquals(1, stale.videoDelayFrames());
        assertEquals(16.7f, stale.audioDelayMs(), 0.1f);
        assertNull(stale.resolvedConfigurationId());
        assertNull(stale.resolvedConfigurationKey());
        assertTrue(stale.fallbacks().contains(FallbackReason.DISPLAY_OBSERVATION_STALE));
    }

    @Test public void previousGenerationAndIncompatibleActiveModeImmediatelyStopActiveMotion() {
        EffectiveVideoConfig oldGeneration = resolve(SourceTiming.NTSC_60_0988,
                observation(4L, NOW - 100L, 4_000L, mode120()), 5L,
                RuntimeTemporalState.MOTION_COMPENSATING);
        assertEquals(RuntimeTemporalState.BUFFERED_NATIVE_HOLD,
                oldGeneration.runtimeTemporalState());
        assertTrue(oldGeneration.fallbacks().contains(
                FallbackReason.DISPLAY_OBSERVATION_STALE));

        EffectiveVideoConfig rejected = resolve(SourceTiming.NTSC_60_0988,
                observation(5L, NOW - 100L, 4_000L, mode60()), 5L,
                RuntimeTemporalState.MOTION_COMPENSATING);
        assertEquals(RuntimeTemporalState.BUFFERED_NATIVE_HOLD,
                rejected.runtimeTemporalState());
        assertTrue(rejected.fallbacks().contains(FallbackReason.DISPLAY_MODE_REJECTED));
    }

    private static EffectiveVideoConfig resolve(SourceTiming sourceTiming,
                                                DisplayObservation observation,
                                                long expectedGeneration,
                                                RuntimeTemporalState currentState) {
        DisplayModeCapability mode120 = mode120();
        VideoConfigurationKey key = new VideoConfigurationKey(SourceTiming.NTSC_60_0988,
                2340, 1080, 2, 120_000, TemporalMode.MOTION_INTERPOLATION,
                SpatialMode.SHARP_BILINEAR, PostEffect.NONE, AspectMode.FOUR_BY_THREE);
        CertifiedVideoConfiguration certificate = new CertifiedVideoConfiguration(
                "motion-qualified", key, EvidenceLevel.DEVICE_LAB, "build-profile-hash",
                "motion-hash", EPOCH - 1_000L, EPOCH + 10_000L,
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        DeviceQualityProfile profile = new DeviceQualityProfile("target-phone", "Google",
                "Pixel", "build-fingerprint", "vendor", "renderer", "version",
                "driver-fingerprint", "build-profile-hash",
                Collections.singletonList(certificate), AdaptiveQualityPolicy.empty());
        DeviceIdentity identity = new DeviceIdentity("Google", "Pixel", "build-fingerprint",
                "vendor", "renderer", "version", "driver-fingerprint");
        BootSessionIdentity boot = new BootSessionIdentity(7, null, "boot-a");
        AuthenticatedTimeSample sample = new AuthenticatedTimeSample(
                AuthenticatedTimeSource.ANDROID_NETWORK_TIME, EPOCH, NOW,
                boot.canonicalIdentitySha256(), "provenance");
        PersistedEvidenceClockAnchor anchor = new PersistedEvidenceClockAnchor(
                boot.canonicalIdentitySha256(), sample, EPOCH, EPOCH, NOW, EPOCH,
                Collections.emptyList(), "mac");
        TrustedEvidenceClockSnapshot clock = TrustedEvidenceClock.evaluate(boot,
                PersistedEvidenceClockState.COMPLETE, anchor, null, EPOCH, NOW, EPOCH - 1L);
        BuildAlgorithmAvailability build = new BuildAlgorithmAvailability(false, false, true,
                0, "", "", "", 1, "motion-hash");
        DisplayCapabilities display = new DisplayCapabilities(2340, 1080,
                Arrays.asList(mode60(), mode120()), knownGl());
        RuntimeConstraints constraints = new RuntimeConstraints(sourceTiming, observation,
                expectedGeneration, false, 80, 35.0f, ThermalBand.NONE, true,
                Collections.emptySet(), currentState);
        VideoPreferences request = new VideoPreferences(VideoQualityPreset.CUSTOM,
                new CustomVideoSettings(PhysicalRefreshPolicy.HZ_120,
                        TemporalMode.MOTION_INTERPOLATION, SpatialMode.SHARP_BILINEAR,
                        PostEffect.NONE), true);
        return new DisplayQualityResolver().resolve(request, AspectMode.FOUR_BY_THREE,
                display, build, constraints, identity, profile, clock, NOW);
    }

    private static DisplayObservation observation(long generation, long observedAt,
                                                  long stableFor,
                                                  DisplayModeCapability active) {
        return new DisplayObservation(generation, PhysicalRefreshPolicy.HZ_120, mode120(),
                active, observedAt, stableFor);
    }

    private static DisplayModeCapability mode60() {
        return new DisplayModeCapability(1, 2340, 1080, 60_000);
    }

    private static DisplayModeCapability mode120() {
        return new DisplayModeCapability(2, 2340, 1080, 120_000);
    }

    private static GlCapabilities knownGl() {
        return new GlCapabilities(3, 2, "vendor", "renderer", "version",
                Collections.emptySet(), 8192, true, true, true, true,
                true, true, true);
    }
}
