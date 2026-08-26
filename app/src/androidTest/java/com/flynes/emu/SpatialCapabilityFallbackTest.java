package com.flynes.emu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.settings.AspectMode;
import com.flynes.emu.video.power.ThermalBand;
import com.flynes.emu.video.quality.BuildAlgorithmAvailability;
import com.flynes.emu.video.quality.CustomVideoSettings;
import com.flynes.emu.video.quality.DisplayCapabilities;
import com.flynes.emu.video.quality.DisplayModeCapability;
import com.flynes.emu.video.quality.DisplayObservation;
import com.flynes.emu.video.quality.DisplayQualityResolver;
import com.flynes.emu.video.quality.EffectiveVideoConfig;
import com.flynes.emu.video.quality.FallbackReason;
import com.flynes.emu.video.quality.GlCapabilities;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;
import com.flynes.emu.video.quality.PostEffect;
import com.flynes.emu.video.quality.RuntimeConstraints;
import com.flynes.emu.video.quality.RuntimeTemporalState;
import com.flynes.emu.video.quality.SourceTiming;
import com.flynes.emu.video.quality.SpatialMode;
import com.flynes.emu.video.quality.TemporalMode;
import com.flynes.emu.video.quality.VideoPreferences;
import com.flynes.emu.video.quality.VideoQualityPreset;
import com.flynes.emu.video.status.GlCapabilityProbe;

import java.util.Collections;
import java.util.List;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class SpatialCapabilityFallbackTest {
    @Test
    public void ownedProbePublishesConcreteDriverFacts() {
        GlCapabilityProbe.Snapshot snapshot = new GlCapabilityProbe(
                new GlCapabilityProbe.AndroidBackend()).probe();

        assertEquals(GlCapabilityProbe.State.KNOWN, snapshot.state());
        GlCapabilities gl = snapshot.capabilities();
        assertTrue(gl.activeContextMajorVersion() >= 2);
        assertTrue(gl.maxTextureSize() >= 768);
        assertFalse(gl.vendor().isEmpty());
        assertFalse(gl.renderer().isEmpty());
    }

    @Test
    public void mmpxRequiresHighpAndSufficientTextureSizeButNotFloatTargets() {
        GlCapabilities capable = gl(true, 512, false, false);
        assertTrue(capable.supportsMmpx2x());
        assertFalse(gl(false, 512, false, false).supportsMmpx2x());
        assertFalse(gl(true, 511, false, false).supportsMmpx2x());
    }

    @Test
    public void scaleFxFailsClosedWithoutRenderableAndFilterableFloatIntermediates() {
        GlCapabilities missingFloat = gl(true, 4096, false, false);
        assertFalse(missingFloat.supportsScaleFx3x());
        assertTrue(gl(true, 4096, true, true).supportsScaleFx3x());

        DisplayModeCapability mode = new DisplayModeCapability(1, 2340, 1080, 60_000);
        DisplayCapabilities display = new DisplayCapabilities(2340, 1080,
                List.of(mode), missingFloat);
        VideoPreferences request = new VideoPreferences(VideoQualityPreset.CUSTOM,
                new CustomVideoSettings(PhysicalRefreshPolicy.HZ_60, TemporalMode.NATIVE,
                        SpatialMode.SCALEFX, PostEffect.NONE), true);
        BuildAlgorithmAvailability build = new BuildAlgorithmAvailability(
                false, true, false, 0, "",
                "4f4eb801b2dbcaed0a9669a9deec1a098f3623d8", "scalefx-hash", 0, "");
        RuntimeConstraints constraints = new RuntimeConstraints(SourceTiming.NTSC_60_0988,
                new DisplayObservation(1L, PhysicalRefreshPolicy.HZ_60, mode, mode,
                        9_500L, 4_000L), false, 80, 35.0f, ThermalBand.NONE, true,
                Collections.emptySet(), RuntimeTemporalState.IMMEDIATE_NATIVE);

        EffectiveVideoConfig result = new DisplayQualityResolver().resolve(request,
                AspectMode.FOUR_BY_THREE, display, build, constraints, 10_000L);

        assertEquals(SpatialMode.SHARP_BILINEAR, result.effectiveSpatial());
        assertTrue(result.fallbacks().contains(FallbackReason.GL_CAPABILITY_UNVERIFIED));
    }

    private static GlCapabilities gl(boolean highp, int maxTexture,
                                     boolean halfRenderable, boolean halfFilterable) {
        return new GlCapabilities(2, 0, "vendor", "renderer", "OpenGL ES 2.0",
                Collections.emptySet(), maxTexture, highp, halfRenderable, halfFilterable,
                false, false, false, false);
    }
}
