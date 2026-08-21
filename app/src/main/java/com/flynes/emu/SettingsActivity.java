package com.flynes.emu;

import android.content.Context;
import android.os.Bundle;
import android.view.View;

import androidx.appcompat.app.AppCompatActivity;

import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.SettingsKeys;
import com.flynes.emu.settings.SettingsFragment;
import com.flynes.emu.settings.SettingsRepository;
import com.flynes.emu.settings.SharedPreferencesSettingsStore;
import com.google.android.material.appbar.MaterialToolbar;

/** Scrollable product settings grouped by video, controls, audio, and general behavior. */
public final class SettingsActivity extends AppCompatActivity {
    static final String PREFS = SettingsRepository.PREFERENCES_NAME;
    static final String KEY_HAPTIC_LEVEL = SettingsKeys.HAPTIC_LEVEL;
    static final String KEY_DISTINCT_AB = SettingsKeys.DISTINCT_AB;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        setContentView(R.layout.activity_settings);
        MaterialToolbar toolbar = findViewById(R.id.settings_toolbar);
        toolbar.setNavigationOnClickListener(view -> finish());

        View root = findViewById(R.id.settings_root);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            view.setPadding(insets.getSystemWindowInsetLeft(),
                    insets.getSystemWindowInsetTop(),
                    insets.getSystemWindowInsetRight(),
                    insets.getSystemWindowInsetBottom());
            return insets;
        });
        root.requestApplyInsets();

        if (state == null) {
            getSupportFragmentManager().beginTransaction()
                    .replace(R.id.settings_content, new SettingsFragment())
                    .commit();
        }
    }

    static HapticLevel loadHapticLevel(Context context) {
        return load(context).hapticLevel();
    }

    static boolean loadDistinctAB(Context context) {
        return load(context).distinctABHaptics();
    }

    private static AppSettings load(Context context) {
        return new SettingsRepository(new SharedPreferencesSettingsStore(context)).load();
    }
}
