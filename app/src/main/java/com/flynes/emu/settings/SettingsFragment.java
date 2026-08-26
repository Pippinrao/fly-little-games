package com.flynes.emu.settings;

import android.content.Intent;
import android.os.Bundle;
import android.view.View;

import androidx.appcompat.app.AppCompatDelegate;
import androidx.core.os.LocaleListCompat;
import androidx.preference.ListPreference;
import androidx.preference.Preference;
import androidx.preference.PreferenceFragmentCompat;

import com.flynes.emu.HomeActivity;
import com.flynes.emu.LicensesActivity;
import com.flynes.emu.ControlLayoutActivity;
import com.flynes.emu.R;
import com.flynes.emu.input.GamepadHitMap;
import com.flynes.emu.input.HapticController;

/** Material preference surface backed by the single validated settings repository. */
public final class SettingsFragment extends PreferenceFragmentCompat {
    private static final String ARG_ROOT = "settings.root";

    private SettingsRepository repository;

    public static SettingsFragment newInstance(String rootKey) {
        SettingsFragment fragment = new SettingsFragment();
        Bundle arguments = new Bundle();
        arguments.putString(ARG_ROOT, rootKey);
        fragment.setArguments(arguments);
        return fragment;
    }

    @Override public void onCreatePreferences(Bundle state, String rootKey) {
        getPreferenceManager().setSharedPreferencesName(SettingsRepository.PREFERENCES_NAME);
        String selectedRoot = getArguments() == null ? rootKey : getArguments().getString(ARG_ROOT, rootKey);
        setPreferencesFromResource(R.xml.preferences, selectedRoot);
        repository = new SettingsRepository(new SharedPreferencesSettingsStore(requireContext()));

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
                AppSettings current = settings();
                AppSettings defaults = AppSettings.defaults();
                repository.save(current.toBuilder()
                        .layoutPreset(defaults.layoutPreset())
                        .directionControlMode(defaults.directionControlMode())
                        .buttonScale(defaults.buttonScale())
                        .verticalOffset(defaults.verticalOffset())
                        .controlOpacity(defaults.controlOpacity())
                        .joystickScale(defaults.joystickScale())
                        .deadZone(defaults.deadZone())
                        .hapticLevel(defaults.hapticLevel())
                        .distinctABHaptics(defaults.distinctABHaptics()).build());
                new ControlLayoutRepository(requireContext()).reset();
                requireActivity().recreate();
                return true;
            });
        }
        Preference layoutEditor = findPreference("controls.layout_editor");
        if (layoutEditor != null) layoutEditor.setOnPreferenceClickListener(preference -> {
            startActivity(new Intent(requireContext(), ControlLayoutActivity.class));
            return true;
        });
        Preference hapticPreview = findPreference("controls.haptic_preview");
        if (hapticPreview != null) hapticPreview.setOnPreferenceClickListener(preference -> {
            View host = requireActivity().findViewById(R.id.settings_content);
            HapticController controller = new HapticController(host);
            AppSettings current = settings();
            controller.configure(current.hapticLevel(), current.distinctABHaptics());
            controller.feedback(GamepadHitMap.Control.A);
            host.postDelayed(() -> controller.feedback(GamepadHitMap.Control.B), 260L);
            return true;
        });
        Preference library = findPreference("general.library");
        if (library != null) {
            library.setOnPreferenceClickListener(preference -> {
                Intent sources = new Intent(requireContext(), HomeActivity.class);
                sources.setAction(HomeActivity.ACTION_SHOW_SOURCES);
                startActivity(sources);
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

}
