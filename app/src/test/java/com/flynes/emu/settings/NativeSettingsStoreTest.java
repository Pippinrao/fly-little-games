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

public final class NativeSettingsStoreTest {
    @Test
    public void settingsRepositoryRoundTripsThroughNativeSnapshotBackend() {
        MemoryBackend backend = new MemoryBackend(FlySettingsMapper.toNative(AppSettings.defaults()));
        SettingsRepository repository = new SettingsRepository(new NativeSettingsStore(backend));
        VideoPreferences video = new VideoPreferences(VideoQualityPreset.CUSTOM,
                new CustomVideoSettings(PhysicalRefreshPolicy.HZ_90,
                        TemporalMode.NATIVE, SpatialMode.SCALEFX, PostEffect.NONE), true);
        AppSettings expected = AppSettings.defaults().toBuilder()
                .aspectMode(AspectMode.INTEGER_SCALE).videoPreferences(video)
                .layoutPreset(LayoutPreset.MIRRORED_AB)
                .directionControlMode(DirectionControlMode.JOYSTICK)
                .hapticLevel(HapticLevel.OFF).localeTag("ja-JP")
                .lastPlayedRomId("game:abc").build();

        assertTrue(repository.save(expected));
        assertEquals(expected, repository.load());
        assertEquals(FlySettingsMapper.ASPECT_INTEGER_SCALE, backend.snapshot.aspectMode());
        assertEquals("ja-JP", backend.snapshot.localeTag());
    }

    @Test
    public void failedApplyLeavesPreviousNativeSnapshotUnchanged() {
        MemoryBackend backend = new MemoryBackend(FlySettingsMapper.toNative(AppSettings.defaults()));
        backend.failApply = true;
        SettingsRepository repository = new SettingsRepository(new NativeSettingsStore(backend));
        AppSettings changed = AppSettings.defaults().toBuilder().localeTag("zh-CN").build();

        assertFalse(repository.save(changed));
        assertEquals("system", backend.snapshot.localeTag());
    }

    private static final class MemoryBackend implements NativeSettingsStore.Backend {
        FlySettingsSnapshot snapshot;
        boolean failApply;

        MemoryBackend(FlySettingsSnapshot snapshot) {
            this.snapshot = snapshot;
        }

        @Override public FlySettingsSnapshot get() {
            return snapshot;
        }

        @Override public boolean apply(FlySettingsSnapshot next) {
            if (failApply) return false;
            snapshot = next;
            return true;
        }
    }
}
