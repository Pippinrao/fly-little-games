package com.flynes.emu.video.quality;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.settings.AspectMode;
import com.flynes.emu.video.power.ThermalBand;

import org.junit.Test;

import java.util.Arrays;
import java.util.Collections;

public final class DisplayQualityResolverBaselineTest {
    @Test public void balancedWithoutQualifiedMmpxIsFullyAppliedSharp() {
        EffectiveVideoConfig result = resolve(VideoPreferences.defaults(),
                BuildAlgorithmAvailability.baseOnly(), constraints(activeMode(60_000)));

        assertEquals(PhysicalRefreshPolicy.HZ_60, result.effectiveRefresh());
        assertEquals(TemporalMode.NATIVE, result.effectiveTemporal());
        assertEquals(RuntimeTemporalState.IMMEDIATE_NATIVE, result.runtimeTemporalState());
        assertEquals(SpatialMode.SHARP_BILINEAR, result.effectiveSpatial());
        assertTrue(result.fallbacks().isEmpty());
        assertTrue(result.resolvedConfigurationId().startsWith("builtin:"));
        assertNotNull(result.resolvedConfigurationKey());
        assertEquals(SourceTiming.NTSC_60_0988,
                result.resolvedConfigurationKey().sourceTiming());
    }

    @Test public void bundledMmpxWithUnknownGlCapabilityDoesNotUnlockMmpx() {
        BuildAlgorithmAvailability bundled = new BuildAlgorithmAvailability(
                true, false, false, 1, "mmpx-hash", "", "", 0, "");

        EffectiveVideoConfig result = resolve(VideoPreferences.defaults(), bundled,
                constraints(activeMode(60_000)));

        assertEquals(SpatialMode.SHARP_BILINEAR, result.effectiveSpatial());
        assertTrue(result.fallbacks().isEmpty());
    }

    @Test public void fixedNinetyRequestRetainsSystemReportedSixtyAsFallbackFact() {
        CustomVideoSettings custom = new CustomVideoSettings(PhysicalRefreshPolicy.HZ_90,
                TemporalMode.NATIVE, SpatialMode.SHARP_BILINEAR, PostEffect.NONE);
        VideoPreferences requested = new VideoPreferences(VideoQualityPreset.CUSTOM, custom, true);

        RuntimeConstraints rejected = new RuntimeConstraints(SourceTiming.NTSC_60_0988,
                new DisplayObservation(1L, PhysicalRefreshPolicy.HZ_90, mode(2, 90_000),
                        activeMode(60_000), 9_500L, 4_000L), false, 80, 35.0f,
                ThermalBand.NONE, true, Collections.emptySet(),
                RuntimeTemporalState.IMMEDIATE_NATIVE);
        EffectiveVideoConfig result = resolve(requested, BuildAlgorithmAvailability.baseOnly(),
                rejected);

        assertEquals(90_000, result.requestedDisplayMode().refreshMilliHz());
        assertEquals(60_000, result.systemReportedActiveMode().refreshMilliHz());
        assertTrue(result.fallbacks().contains(FallbackReason.DISPLAY_MODE_REJECTED));
    }

    @Test public void advancedCodeAndGlWithoutCertificateStillFailClosed() {
        CustomVideoSettings custom = new CustomVideoSettings(PhysicalRefreshPolicy.HZ_60,
                TemporalMode.NATIVE, SpatialMode.MMPX, PostEffect.NONE);
        VideoPreferences requested = new VideoPreferences(VideoQualityPreset.CUSTOM, custom, true);
        BuildAlgorithmAvailability bundled = new BuildAlgorithmAvailability(
                true, false, false, 1, "mmpx-hash", "", "", 0, "");
        DisplayCapabilities display = new DisplayCapabilities(2340, 1080,
                Arrays.asList(mode(1, 60_000), mode(2, 90_000), mode(3, 120_000)),
                knownGl());

        EffectiveVideoConfig result = new DisplayQualityResolver().resolve(requested,
                AspectMode.FOUR_BY_THREE, display, bundled,
                constraints(activeMode(60_000)), 10_000L);

        assertEquals(SpatialMode.SHARP_BILINEAR, result.effectiveSpatial());
        assertTrue(result.fallbacks().contains(FallbackReason.CONFIGURATION_UNVERIFIED));
        assertNotNull(result.resolvedConfigurationId());
        assertNotNull(result.resolvedConfigurationKey());
    }

    @Test public void nativeTime120WithoutPhysicalCertificateFallsBackToSixty() {
        CustomVideoSettings custom = new CustomVideoSettings(PhysicalRefreshPolicy.HZ_120,
                TemporalMode.NATIVE, SpatialMode.SHARP_BILINEAR, PostEffect.NONE);
        VideoPreferences requested = new VideoPreferences(VideoQualityPreset.CUSTOM, custom, true);

        EffectiveVideoConfig result = resolve(requested, BuildAlgorithmAvailability.baseOnly(),
                constraints(activeMode(60_000)));

        assertEquals(PhysicalRefreshPolicy.HZ_60, result.effectiveRefresh());
        assertEquals(60_000, result.requestedDisplayMode().refreshMilliHz());
        assertEquals(SpatialMode.SHARP_BILINEAR, result.effectiveSpatial());
        assertTrue(result.fallbacks().contains(FallbackReason.CONFIGURATION_UNVERIFIED));
    }

    @Test public void severeAndCriticalThermalSignalsRequestSafetyWithoutFakingActiveMode() {
        RuntimeConstraints severe = new RuntimeConstraints(SourceTiming.NTSC_60_0988,
                observation(activeMode(120_000)), false, 80, 42.0f, ThermalBand.SEVERE,
                true, Collections.emptySet(), RuntimeTemporalState.IMMEDIATE_NATIVE);
        EffectiveVideoConfig safe = resolve(VideoPreferences.defaults(),
                BuildAlgorithmAvailability.baseOnly(), severe);
        assertEquals(SpatialMode.NEAREST, safe.effectiveSpatial());
        assertEquals(60_000, safe.requestedDisplayMode().refreshMilliHz());
        assertEquals(120_000, safe.systemReportedActiveMode().refreshMilliHz());
        assertTrue(safe.fallbacks().contains(FallbackReason.THERMAL_SAFETY));

        RuntimeConstraints critical = new RuntimeConstraints(SourceTiming.NTSC_60_0988,
                observation(activeMode(120_000)), false, 80, 35.0f, ThermalBand.CRITICAL,
                true, Collections.emptySet(), RuntimeTemporalState.IMMEDIATE_NATIVE);
        assertEquals(SessionSafetyDirective.PAUSE_FOR_CRITICAL_THERMAL,
                resolve(VideoPreferences.defaults(), BuildAlgorithmAvailability.baseOnly(),
                        critical).sessionSafetyDirective());
    }

    private static EffectiveVideoConfig resolve(VideoPreferences requested,
                                                 BuildAlgorithmAvailability build,
                                                 RuntimeConstraints constraints) {
        DisplayCapabilities display = new DisplayCapabilities(2340, 1080,
                Arrays.asList(mode(1, 60_000), mode(2, 90_000), mode(3, 120_000)),
                GlCapabilities.unknown());
        return new DisplayQualityResolver().resolve(requested, AspectMode.FOUR_BY_THREE,
                display, build, constraints, 10_000L);
    }

    private static RuntimeConstraints constraints(DisplayModeCapability active) {
        return new RuntimeConstraints(SourceTiming.NTSC_60_0988, observation(active),
                false, 80, 35.0f, ThermalBand.NONE, true, Collections.emptySet(),
                RuntimeTemporalState.IMMEDIATE_NATIVE);
    }

    private static DisplayObservation observation(DisplayModeCapability active) {
        return new DisplayObservation(1L, PhysicalRefreshPolicy.HZ_60, mode(1, 60_000),
                active, 9_500L, 4_000L);
    }

    private static DisplayModeCapability activeMode(int milliHz) {
        return mode(milliHz == 60_000 ? 1 : milliHz == 90_000 ? 2 : 3, milliHz);
    }

    private static DisplayModeCapability mode(int id, int milliHz) {
        return new DisplayModeCapability(id, 2340, 1080, milliHz);
    }

    private static GlCapabilities knownGl() {
        return new GlCapabilities(3, 2, "vendor", "renderer", "version",
                Collections.emptySet(), 8192, true, true, true, true,
                true, true, true);
    }
}
