package com.flynes.emu.settings;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.Map;

/** Android adapter for the validated settings repository. */
public final class SharedPreferencesSettingsStore implements SettingsStore {
    private final SharedPreferences preferences;

    public SharedPreferencesSettingsStore(Context context) {
        preferences = context.getApplicationContext().getSharedPreferences(
                SettingsRepository.PREFERENCES_NAME, Context.MODE_PRIVATE);
    }

    @Override public String getString(String key, String fallback) {
        Map<String, ?> values = preferences.getAll();
        Object value = values.get(key);
        return value == null ? fallback : String.valueOf(value);
    }

    @Override public int getInt(String key, int fallback) {
        Object value = preferences.getAll().get(key);
        if (value instanceof Integer) return (Integer) value;
        if (value instanceof String) {
            try {
                return Integer.parseInt((String) value);
            } catch (NumberFormatException ignored) {
                return fallback;
            }
        }
        return fallback;
    }

    @Override public boolean getBoolean(String key, boolean fallback) {
        Object value = preferences.getAll().get(key);
        return value instanceof Boolean ? (Boolean) value : fallback;
    }

    @Override public void putString(String key, String value) {
        preferences.edit().putString(key, value).apply();
    }

    @Override public void putInt(String key, int value) {
        preferences.edit().putInt(key, value).apply();
    }

    @Override public void putBoolean(String key, boolean value) {
        preferences.edit().putBoolean(key, value).apply();
    }
}
