package com.flynes.emu;

import android.content.Context;
import android.content.res.ColorStateList;
import android.os.Bundle;
import android.view.View;

import androidx.appcompat.app.AppCompatActivity;

import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.DisplaySettingsFragment;
import com.flynes.emu.settings.SettingsKeys;
import com.flynes.emu.settings.SettingsFragment;
import com.flynes.emu.settings.SettingsRepository;
import com.flynes.emu.settings.SettingsSection;
import com.flynes.emu.settings.SharedPreferencesSettingsStore;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.appbar.MaterialToolbar;

/** Scrollable product settings grouped by video, controls, audio, and general behavior. */
public final class SettingsActivity extends AppCompatActivity {
    private static final String STATE_SECTION = "settings.section";
    private static final String PREF_SECTION = "settings.last_section";
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

        bindSection(R.id.settings_display, SettingsSection.DISPLAY);
        bindSection(R.id.settings_controls_master, SettingsSection.CONTROLS);
        bindSection(R.id.settings_audio_master, SettingsSection.AUDIO);
        bindSection(R.id.settings_game_language, SettingsSection.GAME_LANGUAGE);
        bindSection(R.id.settings_about, SettingsSection.ABOUT);
        String saved = state == null
                ? getPreferences(MODE_PRIVATE).getString(PREF_SECTION, SettingsSection.DISPLAY.name())
                : state.getString(STATE_SECTION, SettingsSection.DISPLAY.name());
        showSection(SettingsSection.valueOf(saved));
    }

    private SettingsSection selected = SettingsSection.DISPLAY;

    @Override protected void onSaveInstanceState(Bundle outState) {
        outState.putString(STATE_SECTION, selected.name());
        super.onSaveInstanceState(outState);
    }

    private void bindSection(int id, SettingsSection section) {
        findViewById(id).setOnClickListener(view -> showSection(section));
    }

    private void showSection(SettingsSection section) {
        selected = section;
        getPreferences(MODE_PRIVATE).edit().putString(PREF_SECTION, section.name()).apply();
        String tag = "settings:" + section.rootKey();
        androidx.fragment.app.Fragment fragment =
                getSupportFragmentManager().findFragmentByTag(tag);
        if (fragment == null) fragment = section == SettingsSection.DISPLAY
                ? new DisplaySettingsFragment() : SettingsFragment.newInstance(section.rootKey());
        androidx.fragment.app.FragmentTransaction transaction =
                getSupportFragmentManager().beginTransaction();
        for (androidx.fragment.app.Fragment existing : getSupportFragmentManager().getFragments()) {
            transaction.hide(existing);
        }
        if (fragment.isAdded()) transaction.show(fragment);
        else transaction.add(R.id.settings_content, fragment, tag);
        transaction.commit();
        int[] ids = {R.id.settings_display, R.id.settings_controls_master, R.id.settings_audio_master,
                R.id.settings_game_language, R.id.settings_about};
        SettingsSection[] sections = SettingsSection.values();
        for (int i = 0; i < ids.length; i++) {
            MaterialButton button = findViewById(ids[i]);
            boolean active = sections[i] == section;
            button.setSelected(active);
            button.setTextColor(getColor(active ? R.color.fly_on_primary : R.color.fly_on_surface));
            button.setBackgroundTintList(ColorStateList.valueOf(
                    getColor(active ? R.color.fly_primary : R.color.fly_surface)));
            button.setContentDescription(button.getText() + (active ? ", " + getString(R.string.selected) : ""));
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
