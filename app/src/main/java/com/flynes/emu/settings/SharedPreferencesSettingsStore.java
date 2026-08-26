package com.flynes.emu.settings;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

/** Android adapter for the validated settings repository. */
public final class SharedPreferencesSettingsStore implements SettingsStore {
    private final SharedPreferences preferences;

    public SharedPreferencesSettingsStore(Context context) {
        preferences = context.getApplicationContext().getSharedPreferences(
                SettingsRepository.PREFERENCES_NAME, Context.MODE_PRIVATE);
    }

    @Override public Map<String, ?> snapshot() {
        return Collections.unmodifiableMap(new LinkedHashMap<>(preferences.getAll()));
    }

    @Override public boolean commit(SettingsBatch batch) {
        SharedPreferences.Editor editor = preferences.edit();
        for (String key : batch.removals()) editor.remove(key);
        for (Map.Entry<String, String> entry : batch.strings().entrySet())
            editor.putString(entry.getKey(), entry.getValue());
        for (Map.Entry<String, Integer> entry : batch.integers().entrySet())
            editor.putInt(entry.getKey(), entry.getValue());
        for (Map.Entry<String, Boolean> entry : batch.booleans().entrySet())
            editor.putBoolean(entry.getKey(), entry.getValue());
        return editor.commit();
    }
}
