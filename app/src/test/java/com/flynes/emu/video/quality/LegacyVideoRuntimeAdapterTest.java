package com.flynes.emu.video.quality;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.settings.FilterMode;
import com.flynes.emu.video.RefreshMode;

import org.junit.Test;

import java.util.Collections;

public final class LegacyVideoRuntimeAdapterTest {
    @Test public void projectsPresetModesWithoutMutatingPreservedCustomSettings() {
        CustomVideoSettings preserved = new CustomVideoSettings(
                PhysicalRefreshPolicy.HZ_120, TemporalMode.MOTION_INTERPOLATION,
                SpatialMode.MMPX, PostEffect.CRT);

        LegacyVideoRuntimeAdapter power = LegacyVideoRuntimeAdapter.project(
                new VideoPreferences(VideoQualityPreset.POWER_SAVER, preserved, true));
        assertEquals(RefreshMode.HZ_60, power.displayRefresh());
        assertEquals(FilterMode.NEAREST, power.rendererFilter());
        assertEquals(RuntimeTemporalState.APPLIED, power.temporalState());

        LegacyVideoRuntimeAdapter balanced = LegacyVideoRuntimeAdapter.project(
                new VideoPreferences(VideoQualityPreset.BALANCED, preserved, true));
        assertEquals(RefreshMode.HZ_60, balanced.displayRefresh());
        assertEquals(FilterMode.SHARP_BILINEAR, balanced.rendererFilter());

        LegacyVideoRuntimeAdapter extreme = LegacyVideoRuntimeAdapter.project(
                new VideoPreferences(VideoQualityPreset.EXTREME, preserved, true));
        assertEquals(RefreshMode.HZ_60, extreme.displayRefresh());
        assertEquals(FilterMode.SHARP_BILINEAR, extreme.rendererFilter());
        assertEquals(RuntimeTemporalState.FALLBACK, extreme.temporalState());
        assertEquals(preserved, extreme.requested().custom());
    }

    @Test public void customProjectionKeepsFollowSystemDistinctAndFallsBackHonestly() {
        CustomVideoSettings custom = new CustomVideoSettings(
                PhysicalRefreshPolicy.FOLLOW_SYSTEM, TemporalMode.MOTION_INTERPOLATION,
                SpatialMode.SCALEFX, PostEffect.CRT);
        LegacyVideoRuntimeAdapter projected = LegacyVideoRuntimeAdapter.project(
                new VideoPreferences(VideoQualityPreset.CUSTOM, custom, true));

        assertTrue(projected.followsSystemRefresh());
        assertEquals(null, projected.displayRefresh());
        assertEquals(FilterMode.CRT, projected.rendererFilter());
        assertEquals(TemporalMode.MOTION_INTERPOLATION, projected.requestedTemporalMode());
        assertEquals(TemporalMode.NATIVE, projected.runtimeTemporalMode());
        assertEquals(RuntimeTemporalState.FALLBACK, projected.temporalState());
    }

    @Test public void legacyAutoAndFixedPoliciesProjectExactly() {
        assertEquals(RefreshMode.AUTO, projectRefresh(PhysicalRefreshPolicy.LEGACY_AUTO_INTEGER_MULTIPLE));
        assertEquals(RefreshMode.HZ_60, projectRefresh(PhysicalRefreshPolicy.HZ_60));
        assertEquals(RefreshMode.HZ_90, projectRefresh(PhysicalRefreshPolicy.HZ_90));
        assertEquals(RefreshMode.HZ_120, projectRefresh(PhysicalRefreshPolicy.HZ_120));
        assertFalse(project(PhysicalRefreshPolicy.HZ_60).followsSystemRefresh());
    }

    @Test public void effectiveProjectionUsesResolvedAxesInsteadOfRequestedAdvancedAxes() {
        VideoPreferences requested = new VideoPreferences(VideoQualityPreset.CUSTOM,
                new CustomVideoSettings(PhysicalRefreshPolicy.HZ_120,
                        TemporalMode.MOTION_INTERPOLATION, SpatialMode.SCALEFX,
                        PostEffect.CRT), true);
        DisplayModeCapability mode = new DisplayModeCapability(7, 2340, 1080, 120_000);
        VideoConfigurationKey key = new VideoConfigurationKey(SourceTiming.NTSC_60_0988,
                2340, 1080, 7, 120_000, TemporalMode.NATIVE, SpatialMode.MMPX,
                PostEffect.NONE, com.flynes.emu.settings.AspectMode.FOUR_BY_THREE);
        EffectiveVideoConfig effective = new EffectiveVideoConfig(requested,
                "certified:mmpx", key, PhysicalRefreshPolicy.HZ_120, mode, mode,
                TemporalMode.NATIVE, RuntimeTemporalState.IMMEDIATE_NATIVE,
                SpatialMode.MMPX, PostEffect.NONE, 0, 0f,
                SessionSafetyDirective.NONE, Collections.emptyList());

        LegacyVideoRuntimeAdapter projected = LegacyVideoRuntimeAdapter.project(effective);

        assertEquals(FilterMode.MMPX, projected.rendererFilter());
        assertEquals(RefreshMode.HZ_120, projected.displayRefresh());
        assertEquals(TemporalMode.MOTION_INTERPOLATION, projected.requestedTemporalMode());
        assertEquals(TemporalMode.NATIVE, projected.runtimeTemporalMode());
        assertEquals(RuntimeTemporalState.IMMEDIATE_NATIVE, projected.temporalState());
    }

    private static RefreshMode projectRefresh(PhysicalRefreshPolicy policy) {
        return project(policy).displayRefresh();
    }

    private static LegacyVideoRuntimeAdapter project(PhysicalRefreshPolicy policy) {
        CustomVideoSettings custom = new CustomVideoSettings(policy, TemporalMode.NATIVE,
                SpatialMode.NEAREST, PostEffect.NONE);
        return LegacyVideoRuntimeAdapter.project(
                new VideoPreferences(VideoQualityPreset.CUSTOM, custom, true));
    }
}
