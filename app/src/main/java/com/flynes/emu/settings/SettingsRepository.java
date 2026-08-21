package com.flynes.emu.settings;

import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.video.RefreshMode;

/** Versioned settings loader that validates all untrusted persisted values. */
public final class SettingsRepository {
    public static final int SCHEMA_VERSION = 1;
    public static final String PREFERENCES_NAME = "flynes_settings";

    private final SettingsStore store;

    public SettingsRepository(SettingsStore store) {
        if (store == null) throw new IllegalArgumentException("store must not be null");
        this.store = store;
    }

    public AppSettings load() {
        AppSettings defaults = AppSettings.defaults();
        AppSettings settings = defaults.toBuilder()
                .aspectMode(enumValue(SettingsKeys.ASPECT, AspectMode.class, defaults.aspectMode()))
                .filterMode(enumValue(SettingsKeys.FILTER, FilterMode.class, defaults.filterMode()))
                .refreshMode(enumValue(SettingsKeys.REFRESH, RefreshMode.class, defaults.refreshMode()))
                .layoutPreset(enumValue(SettingsKeys.LAYOUT, LayoutPreset.class,
                        defaults.layoutPreset()))
                .buttonScale(floatValue(SettingsKeys.BUTTON_SCALE, defaults.buttonScale()))
                .verticalOffset(floatValue(SettingsKeys.VERTICAL_OFFSET,
                        defaults.verticalOffset()))
                .controlOpacity(floatValue(SettingsKeys.CONTROL_OPACITY,
                        defaults.controlOpacity()))
                .joystickScale(floatValue(SettingsKeys.JOYSTICK_SCALE,
                        defaults.joystickScale()))
                .deadZone(floatValue(SettingsKeys.DEAD_ZONE, defaults.deadZone()))
                .hapticLevel(enumValue(SettingsKeys.HAPTIC_LEVEL, HapticLevel.class,
                        defaults.hapticLevel()))
                .distinctABHaptics(store.getBoolean(SettingsKeys.DISTINCT_AB,
                        defaults.distinctABHaptics()))
                .audioEnabled(store.getBoolean(SettingsKeys.AUDIO_ENABLED,
                        defaults.audioEnabled()))
                .audioFocusPolicy(enumValue(SettingsKeys.AUDIO_FOCUS, AudioFocusPolicy.class,
                        defaults.audioFocusPolicy()))
                .localeTag(store.getString(SettingsKeys.LOCALE_TAG, defaults.localeTag()))
                .autosaveEnabled(store.getBoolean(SettingsKeys.AUTOSAVE,
                        defaults.autosaveEnabled()))
                .lastPlayedRomId(store.getString(SettingsKeys.LAST_ROM,
                        defaults.lastPlayedRomId()))
                .build();

        if (store.getInt(SettingsKeys.SCHEMA, 0) != SCHEMA_VERSION) {
            save(settings);
        }
        return settings;
    }

    public void save(AppSettings settings) {
        if (settings == null) throw new IllegalArgumentException("settings must not be null");
        store.putString(SettingsKeys.ASPECT, settings.aspectMode().name());
        store.putString(SettingsKeys.FILTER, settings.filterMode().name());
        store.putString(SettingsKeys.REFRESH, settings.refreshMode().name());
        store.putString(SettingsKeys.LAYOUT, settings.layoutPreset().name());
        store.putString(SettingsKeys.BUTTON_SCALE, Float.toString(settings.buttonScale()));
        store.putString(SettingsKeys.VERTICAL_OFFSET, Float.toString(settings.verticalOffset()));
        store.putString(SettingsKeys.CONTROL_OPACITY, Float.toString(settings.controlOpacity()));
        store.putString(SettingsKeys.JOYSTICK_SCALE, Float.toString(settings.joystickScale()));
        store.putString(SettingsKeys.DEAD_ZONE, Float.toString(settings.deadZone()));
        store.putString(SettingsKeys.HAPTIC_LEVEL, settings.hapticLevel().name());
        store.putBoolean(SettingsKeys.DISTINCT_AB, settings.distinctABHaptics());
        store.putBoolean(SettingsKeys.AUDIO_ENABLED, settings.audioEnabled());
        store.putString(SettingsKeys.AUDIO_FOCUS, settings.audioFocusPolicy().name());
        store.putString(SettingsKeys.LOCALE_TAG, settings.localeTag());
        store.putBoolean(SettingsKeys.AUTOSAVE, settings.autosaveEnabled());
        store.putString(SettingsKeys.LAST_ROM, settings.lastPlayedRomId());
        // Schema is deliberately written last so an interrupted migration is retried.
        store.putInt(SettingsKeys.SCHEMA, SCHEMA_VERSION);
    }

    private float floatValue(String key, float fallback) {
        try {
            return Float.parseFloat(store.getString(key, Float.toString(fallback)));
        } catch (NumberFormatException ignored) {
            return fallback;
        }
    }

    private <T extends Enum<T>> T enumValue(String key, Class<T> type, T fallback) {
        try {
            return Enum.valueOf(type, store.getString(key, fallback.name()));
        } catch (IllegalArgumentException | NullPointerException ignored) {
            return fallback;
        }
    }
}
