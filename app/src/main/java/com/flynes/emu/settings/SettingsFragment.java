package com.flynes.emu.settings;

import android.content.Intent;
import android.os.Bundle;
import android.view.Display;
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

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

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
        configureDisplay();

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

    private void configureDisplay() {
        ListPreference refresh = findPreference(SettingsKeys.REFRESH);
        Preference status = findPreference("display.status");
        if (refresh == null && status == null) return;
        Display display = requireActivity().getWindowManager().getDefaultDisplay();
        Set<Integer> supported = new LinkedHashSet<>();
        for (Display.Mode mode : display.getSupportedModes()) {
            int hz = Math.round(mode.getRefreshRate());
            if (hz == 60 || hz == 90 || hz == 120) supported.add(hz);
        }
        if (refresh != null) {
            List<String> labels = new ArrayList<>();
            List<String> values = new ArrayList<>();
            labels.add(getString(R.string.refresh_auto)); values.add("AUTO");
            for (int hz : supported) { labels.add(hz + " Hz"); values.add("HZ_" + hz); }
            refresh.setEntries(labels.toArray(new String[0]));
            refresh.setEntryValues(values.toArray(new String[0]));
            refresh.setOnPreferenceChangeListener((preference, value) -> {
                Display.Mode now = requireActivity().getWindowManager().getDefaultDisplay().getMode();
                DisplayStatus next = statusForMode(String.valueOf(value), supported, now.getRefreshRate());
                new DisplayStatusRepository(requireContext()).save(next);
                renderStatus(status, next);
                return true;
            });
        }
        if (status != null) {
            float actual = display.getMode().getRefreshRate();
            DisplayStatus recorded = new DisplayStatusRepository(requireContext()).load();
            DisplayStatus current = recorded.requestedHz() > 0f
                    ? new DisplayStatus(recorded.requestedHz(), actual, recorded.fallbackReason())
                    : statusForMode(settings().refreshMode().name(), supported, actual);
            new DisplayStatusRepository(requireContext()).save(current);
            renderStatus(status, current);
        }
    }

    private DisplayStatus statusForMode(String mode, Set<Integer> supported, float actual) {
        if ("AUTO".equals(mode)) {
            int best = Math.round(actual);
            for (int hz : supported) if (hz > best) best = hz;
            return new DisplayStatus(best, actual, "AUTO_BEST_SUPPORTED");
        }
        float requested = Float.parseFloat(mode.substring(3));
        return new DisplayStatus(requested, actual,
                supported.contains(Math.round(requested)) ? "" : "MODE_UNAVAILABLE");
    }

    private void renderStatus(Preference preference, DisplayStatus value) {
        String reason;
        if ("AUTO_BEST_SUPPORTED".equals(value.fallbackReason()))
            reason = getString(R.string.display_auto_policy);
        else if ("MODE_UNAVAILABLE".equals(value.fallbackReason()))
            reason = getString(R.string.display_fallback_unavailable);
        else if ("AUTO_SYSTEM_FALLBACK".equals(value.fallbackReason()))
            reason = getString(R.string.display_auto_system_fallback);
        else reason = getString(R.string.display_no_fallback);
        preference.setSummary(getString(R.string.display_status_summary,
                Math.round(value.requestedHz()), Math.round(value.actualHz()), reason));
    }
}
