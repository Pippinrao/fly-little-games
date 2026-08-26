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

public final class SettingsRepositoryTest {
    @Test public void defaultsMatchApprovedDesign() {
        AppSettings settings = new SettingsRepository(
                new VideoSettingsMigrationTest.AtomicMemoryStore()).load();
        assertEquals(AspectMode.FOUR_BY_THREE, settings.aspectMode());
        assertEquals(VideoQualityPreset.BALANCED, settings.videoPreferences().preset());
        assertEquals(PhysicalRefreshPolicy.HZ_60,
                settings.videoPreferences().custom().refreshPolicy());
        assertEquals(SpatialMode.SHARP_BILINEAR,
                settings.videoPreferences().custom().spatialMode());
        assertTrue(settings.videoPreferences().adaptiveProtection());
    }

    @Test public void roundTripPersistsEverySetting() {
        VideoSettingsMigrationTest.AtomicMemoryStore store = new VideoSettingsMigrationTest.AtomicMemoryStore();
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
        SettingsRepository repository = new SettingsRepository(store);
        assertTrue(repository.save(expected));
        assertEquals(expected, repository.load());
        assertEquals(Integer.valueOf(4), store.values.get(SettingsKeys.SCHEMA));
    }

    @Test public void migrationKeepsLegacyNonVideoPreferences() {
        VideoSettingsMigrationTest.AtomicMemoryStore store = new VideoSettingsMigrationTest.AtomicMemoryStore()
                .put(SettingsKeys.HAPTIC_LEVEL, "STANDARD").put(SettingsKeys.DISTINCT_AB, false);
        AppSettings settings = new SettingsRepository(store).load();
        assertEquals(HapticLevel.STANDARD, settings.hapticLevel());
        assertFalse(settings.distinctABHaptics());
    }

    @Test public void controlsResetShapeCanPreserveVideoAndGeneralSettings() {
        VideoPreferences video = new VideoPreferences(VideoQualityPreset.EXTREME,
                CustomVideoSettings.defaults(), false);
        AppSettings current = AppSettings.defaults().toBuilder().videoPreferences(video)
                .localeTag("zh-CN").audioEnabled(false).buttonScale(1.4f).build();
        AppSettings defaults = AppSettings.defaults();
        AppSettings reset = current.toBuilder().buttonScale(defaults.buttonScale())
                .hapticLevel(defaults.hapticLevel()).build();
        assertEquals(video, reset.videoPreferences());
        assertEquals("zh-CN", reset.localeTag());
        assertFalse(reset.audioEnabled());
    }
}
