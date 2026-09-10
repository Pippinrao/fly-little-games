package com.flynes.emu.settings;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.input.DirectionControlMode;
import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.video.quality.CustomVideoSettings;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;
import com.flynes.emu.video.quality.PostEffect;
import com.flynes.emu.video.quality.SpatialMode;
import com.flynes.emu.video.quality.TemporalMode;
import com.flynes.emu.video.quality.VideoPreferences;
import com.flynes.emu.video.quality.VideoQualityPreset;

import org.junit.Test;

public final class FlySettingsMapperTest {
    @Test
    public void frozenCEnumsAreOneBasedNotJavaOrdinals() {
        assertEquals(0, AspectMode.FOUR_BY_THREE.ordinal());
        assertEquals(1, FlySettingsMapper.ASPECT_FOUR_BY_THREE);
        assertEquals(2, FlySettingsMapper.ASPECT_SQUARE_PIXELS);
        assertEquals(3, FlySettingsMapper.ASPECT_INTEGER_SCALE);
        assertEquals(1, FlySettingsMapper.VIDEO_QUALITY_POWER_SAVER);
        assertEquals(4, FlySettingsMapper.VIDEO_QUALITY_CUSTOM);
        assertEquals(1, FlySettingsMapper.REFRESH_FOLLOW_SYSTEM);
        assertEquals(2, FlySettingsMapper.REFRESH_LEGACY_AUTO_INTEGER_MULTIPLE);
        assertEquals(5, FlySettingsMapper.REFRESH_HZ_120);
        assertEquals(1, FlySettingsMapper.TEMPORAL_NATIVE);
        assertEquals(2, FlySettingsMapper.TEMPORAL_MOTION_INTERPOLATION);
        assertEquals(1, FlySettingsMapper.SPATIAL_NEAREST);
        assertEquals(4, FlySettingsMapper.SPATIAL_SCALEFX);
        assertEquals(1, FlySettingsMapper.POST_EFFECT_NONE);
        assertEquals(2, FlySettingsMapper.POST_EFFECT_CRT);
        assertEquals(1, FlySettingsMapper.LAYOUT_STANDARD_BA);
        assertEquals(2, FlySettingsMapper.LAYOUT_MIRRORED_AB);
        assertEquals(1, FlySettingsMapper.DIRECTION_JOYSTICK);
        assertEquals(3, FlySettingsMapper.DIRECTION_DPAD);
        assertEquals(1, FlySettingsMapper.HAPTIC_OFF);
        assertEquals(4, FlySettingsMapper.HAPTIC_STRONG);
        assertEquals(1, FlySettingsMapper.AUDIO_FOCUS_PAUSE);
        assertEquals(3, FlySettingsMapper.AUDIO_FOCUS_IGNORE);
    }

    @Test
    public void roundTripMapsEveryFieldThroughNativeSnapshot() {
        VideoPreferences video = new VideoPreferences(VideoQualityPreset.CUSTOM,
                new CustomVideoSettings(PhysicalRefreshPolicy.HZ_120,
                        TemporalMode.MOTION_INTERPOLATION, SpatialMode.MMPX, PostEffect.CRT), false);
        AppSettings expected = AppSettings.defaults().toBuilder()
                .aspectMode(AspectMode.SQUARE_PIXELS).videoPreferences(video)
                .layoutPreset(LayoutPreset.MIRRORED_AB)
                .directionControlMode(DirectionControlMode.DPAD).buttonScale(1.25f)
                .verticalOffset(-0.15f).controlOpacity(0.72f).joystickScale(1.1f)
                .deadZone(0.25f).hapticLevel(HapticLevel.STRONG)
                .distinctABHaptics(false).audioEnabled(false)
                .audioFocusPolicy(AudioFocusPolicy.DUCK).localeTag("zh-CN")
                .autosaveEnabled(false).lastPlayedRomId("ABC123").build();

        FlySettingsSnapshot nativeSnapshot = FlySettingsMapper.toNative(expected);
        assertEquals(FlySettingsMapper.ASPECT_SQUARE_PIXELS, nativeSnapshot.aspectMode());
        assertEquals(FlySettingsMapper.VIDEO_QUALITY_CUSTOM, nativeSnapshot.videoQualityPreset());
        assertEquals(FlySettingsMapper.REFRESH_HZ_120, nativeSnapshot.customRefreshPolicy());
        assertEquals(0, nativeSnapshot.adaptiveProtection());
        assertEquals("zh-CN", nativeSnapshot.localeTag());
        assertEquals(expected, FlySettingsMapper.fromNative(nativeSnapshot));
    }
}
