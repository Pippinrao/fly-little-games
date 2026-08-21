package com.flynes.emu.settings;

import android.content.Intent;
import android.os.Bundle;

import androidx.appcompat.app.AppCompatDelegate;
import androidx.core.os.LocaleListCompat;
import androidx.preference.ListPreference;
import androidx.preference.Preference;
import androidx.preference.PreferenceFragmentCompat;
import androidx.preference.SeekBarPreference;

import com.flynes.emu.GameLibraryActivity;
import com.flynes.emu.LicensesActivity;
import com.flynes.emu.R;

/** Material preference surface backed by the single validated settings repository. */
public final class SettingsFragment extends PreferenceFragmentCompat {
    private static final String UI_BUTTON_PERCENT = "ui.controls.button_percent";
    private static final String UI_VERTICAL_PERCENT = "ui.controls.vertical_percent";
    private static final String UI_OPACITY_PERCENT = "ui.controls.opacity_percent";
    private static final String UI_JOYSTICK_PERCENT = "ui.controls.joystick_percent";
    private static final String UI_DEAD_ZONE_PERCENT = "ui.controls.dead_zone_percent";

    private SettingsRepository repository;

    @Override public void onCreatePreferences(Bundle state, String rootKey) {
        getPreferenceManager().setSharedPreferencesName(SettingsRepository.PREFERENCES_NAME);
        setPreferencesFromResource(R.xml.preferences, rootKey);
        repository = new SettingsRepository(new SharedPreferencesSettingsStore(requireContext()));
        AppSettings settings = repository.load();

        bindSeek(UI_BUTTON_PERCENT, Math.round(settings.buttonScale() * 100f),
                value -> settings().toBuilder().buttonScale(value / 100f).build());
        bindSeek(UI_VERTICAL_PERCENT, Math.round(settings.verticalOffset() * 100f),
                value -> settings().toBuilder().verticalOffset(value / 100f).build());
        bindSeek(UI_OPACITY_PERCENT, Math.round(settings.controlOpacity() * 100f),
                value -> settings().toBuilder().controlOpacity(value / 100f).build());
        bindSeek(UI_JOYSTICK_PERCENT, Math.round(settings.joystickScale() * 100f),
                value -> settings().toBuilder().joystickScale(value / 100f).build());
        bindSeek(UI_DEAD_ZONE_PERCENT, Math.round(settings.deadZone() * 100f),
                value -> settings().toBuilder().deadZone(value / 100f).build());

        ListPreference language = findPreference(SettingsKeys.LOCALE_TAG);
        if (language != null) {
            language.setOnPreferenceChangeListener((preference, value) -> {
                String tag = String.valueOf(value);
                repository.save(settings().toBuilder().localeTag(tag).build());
                AppCompatDelegate.setApplicationLocales("system".equals(tag)
                        ? LocaleListCompat.getEmptyLocaleList()
                        : LocaleListCompat.forLanguageTags(tag));
                return false;
            });
        }

        Preference reset = findPreference("controls.reset");
        if (reset != null) {
            reset.setOnPreferenceClickListener(preference -> {
                repository.save(AppSettings.defaults());
                requireActivity().recreate();
                return true;
            });
        }
        Preference library = findPreference("general.library");
        if (library != null) {
            library.setOnPreferenceClickListener(preference -> {
                startActivity(new Intent(requireContext(), GameLibraryActivity.class));
                return true;
            });
        }
        Preference licenses = findPreference("general.licenses");
        if (licenses != null) {
            licenses.setOnPreferenceClickListener(preference -> {
                startActivity(new Intent(requireContext(), LicensesActivity.class));
                return true;
            });
        }
    }

    private AppSettings settings() {
        return repository.load();
    }

    private void bindSeek(String key, int initial, SettingTransform transform) {
        SeekBarPreference preference = findPreference(key);
        if (preference == null) return;
        preference.setValue(initial);
        preference.setOnPreferenceChangeListener((ignored, value) -> {
            repository.save(transform.apply((Integer) value));
            return true;
        });
    }

    private interface SettingTransform {
        AppSettings apply(int value);
    }
}
