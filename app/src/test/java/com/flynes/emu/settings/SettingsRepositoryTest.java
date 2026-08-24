package com.flynes.emu.settings;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.input.DirectionControlMode;
import com.flynes.emu.video.RefreshMode;

import org.junit.Test;

import java.util.HashMap;
import java.util.Map;

public final class SettingsRepositoryTest {
    @Test
    public void defaultsMatchApprovedDesign() {
        AppSettings settings = new SettingsRepository(new MemoryStore()).load();

        assertEquals(AspectMode.FOUR_BY_THREE, settings.aspectMode());
        assertEquals(FilterMode.EDGE_ENHANCED, settings.filterMode());
        assertEquals(RefreshMode.AUTO, settings.refreshMode());
        assertEquals(HapticLevel.LIGHT, settings.hapticLevel());
        assertTrue(settings.distinctABHaptics());
        assertEquals(LayoutPreset.STANDARD_BA, settings.layoutPreset());
        assertEquals(DirectionControlMode.JOYSTICK, settings.directionControlMode());
        assertEquals(.22f, settings.deadZone(), .0001f);
        assertTrue(settings.audioEnabled());
        assertTrue(settings.autosaveEnabled());
        assertEquals("system", settings.localeTag());
    }

    @Test
    public void roundTripPersistsEverySetting() {
        MemoryStore store = new MemoryStore();
        SettingsRepository repository = new SettingsRepository(store);
        AppSettings expected = AppSettings.defaults().toBuilder()
                .aspectMode(AspectMode.SQUARE_PIXELS)
                .filterMode(FilterMode.NEAREST)
                .refreshMode(RefreshMode.HZ_120)
                .layoutPreset(LayoutPreset.MIRRORED_AB)
                .directionControlMode(DirectionControlMode.DPAD)
                .buttonScale(1.25f)
                .verticalOffset(-0.15f)
                .controlOpacity(0.72f)
                .joystickScale(1.1f)
                .deadZone(0.25f)
                .hapticLevel(HapticLevel.STRONG)
                .distinctABHaptics(false)
                .audioEnabled(false)
                .audioFocusPolicy(AudioFocusPolicy.DUCK)
                .localeTag("zh-CN")
                .autosaveEnabled(false)
                .lastPlayedRomId("ABC123")
                .build();

        repository.save(expected);
        AppSettings actual = repository.load();

        assertEquals(expected, actual);
        assertEquals(SettingsRepository.SCHEMA_VERSION,
                store.getInt(SettingsKeys.SCHEMA, 0));
    }

    @Test
    public void invalidAndOutOfRangeValuesFallBackOrClampWithoutCrashing() {
        MemoryStore store = new MemoryStore()
                .put(SettingsKeys.SCHEMA, 0)
                .put(SettingsKeys.ASPECT, "BROKEN")
                .put(SettingsKeys.HAPTIC_LEVEL, "UNKNOWN")
                .put(SettingsKeys.BUTTON_SCALE, "99.0")
                .put(SettingsKeys.CONTROL_OPACITY, "-4")
                .put(SettingsKeys.LOCALE_TAG, "");

        AppSettings settings = new SettingsRepository(store).load();

        assertEquals(AspectMode.FOUR_BY_THREE, settings.aspectMode());
        assertEquals(HapticLevel.LIGHT, settings.hapticLevel());
        assertEquals(AppSettings.MAX_BUTTON_SCALE, settings.buttonScale(), 0.0001f);
        assertEquals(AppSettings.MIN_CONTROL_OPACITY, settings.controlOpacity(), 0.0001f);
        assertEquals("system", settings.localeTag());
        assertEquals(SettingsRepository.SCHEMA_VERSION,
                store.getInt(SettingsKeys.SCHEMA, 0));
    }

    @Test
    public void migrationKeepsLegacyHapticPreferences() {
        MemoryStore store = new MemoryStore()
                .put(SettingsKeys.HAPTIC_LEVEL, HapticLevel.STANDARD.name())
                .put(SettingsKeys.DISTINCT_AB, false);

        AppSettings settings = new SettingsRepository(store).load();

        assertEquals(HapticLevel.STANDARD, settings.hapticLevel());
        assertFalse(settings.distinctABHaptics());
    }

    @Test
    public void migrationMapsLegacyFilterNamesToHonestGpuModes() {
        MemoryStore hq4x = new MemoryStore()
                .put(SettingsKeys.SCHEMA, 1).put(SettingsKeys.FILTER, "HQ4X");
        MemoryStore smooth = new MemoryStore()
                .put(SettingsKeys.SCHEMA, 1).put(SettingsKeys.FILTER, "SMOOTH");

        assertEquals(FilterMode.EDGE_ENHANCED,
                new SettingsRepository(hq4x).load().filterMode());
        assertEquals(FilterMode.SHARP_BILINEAR,
                new SettingsRepository(smooth).load().filterMode());
        assertEquals("EDGE_ENHANCED", hq4x.getString(SettingsKeys.FILTER, ""));
        assertEquals("SHARP_BILINEAR", smooth.getString(SettingsKeys.FILTER, ""));
    }

    private static final class MemoryStore implements SettingsStore {
        private final Map<String, Object> values = new HashMap<>();

        MemoryStore put(String key, Object value) {
            values.put(key, value);
            return this;
        }

        @Override public String getString(String key, String fallback) {
            Object value = values.get(key);
            return value instanceof String ? (String) value : fallback;
        }

        @Override public int getInt(String key, int fallback) {
            Object value = values.get(key);
            return value instanceof Integer ? (Integer) value : fallback;
        }

        @Override public boolean getBoolean(String key, boolean fallback) {
            Object value = values.get(key);
            return value instanceof Boolean ? (Boolean) value : fallback;
        }

        @Override public void putString(String key, String value) {
            values.put(key, value);
        }

        @Override public void putInt(String key, int value) {
            values.put(key, value);
        }

        @Override public void putBoolean(String key, boolean value) {
            values.put(key, value);
        }
    }
}
