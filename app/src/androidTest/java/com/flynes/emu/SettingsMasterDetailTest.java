package com.flynes.emu;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.junit.Assert.assertTrue;

import android.content.Intent;
import android.graphics.Rect;
import android.view.View;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class SettingsMasterDetailTest {
    @Test public void resetControlsRestoresLayoutAndFeedbackDefaultsTogether() {
        android.content.Context context=ApplicationProvider.getApplicationContext();
        com.flynes.emu.settings.SettingsRepository settings=new com.flynes.emu.settings.SettingsRepository(
                new com.flynes.emu.settings.SharedPreferencesSettingsStore(context));
        settings.save(com.flynes.emu.settings.AppSettings.defaults().toBuilder()
                .hapticLevel(com.flynes.emu.input.HapticLevel.STRONG).distinctABHaptics(false).build());
        new com.flynes.emu.settings.ControlLayoutRepository(context).save(
                com.flynes.emu.input.ControlLayoutV2.recommended().move(com.flynes.emu.input.ControlLayoutV2.Element.A,.5f,.5f));
        try(ActivityScenario<SettingsActivity> scenario=ActivityScenario.launch(SettingsActivity.class)) {
            scenario.onActivity(activity->{
                activity.findViewById(R.id.settings_controls_master).performClick();
                activity.getSupportFragmentManager().executePendingTransactions();
                com.flynes.emu.settings.SettingsFragment fragment=
                        (com.flynes.emu.settings.SettingsFragment) visibleFragment(activity);
                fragment.findPreference("controls.reset").performClick();
            });
            scenario.onActivity(activity->{
                org.junit.Assert.assertEquals(com.flynes.emu.input.ControlLayoutV2.recommended(),
                        new com.flynes.emu.settings.ControlLayoutRepository(activity).load());
                com.flynes.emu.settings.AppSettings actual=new com.flynes.emu.settings.SettingsRepository(
                        new com.flynes.emu.settings.SharedPreferencesSettingsStore(activity)).load();
                org.junit.Assert.assertEquals(com.flynes.emu.input.HapticLevel.LIGHT,actual.hapticLevel());
                org.junit.Assert.assertTrue(actual.distinctABHaptics());
            });
        }
    }
    @Test public void allFiveMasterSectionsSwitchTheDetailPane() {
        try (ActivityScenario<SettingsActivity> scenario = ActivityScenario.launch(SettingsActivity.class)) {
            scenario.onActivity(activity -> {
                assertDisplaySection(activity);
                androidx.fragment.app.Fragment display = visibleFragment(activity);
                assertSection(activity, R.id.settings_controls_master, "controls.layout_editor");
                assertSection(activity, R.id.settings_audio_master, "audio.enabled");
                assertSection(activity, R.id.settings_game_language, "general.locale");
                assertSection(activity, R.id.settings_about, "general.licenses");
                assertDisplaySection(activity);
                org.junit.Assert.assertSame(display, visibleFragment(activity));
            });
        }
    }

    @Test public void twoHundredPercentFontKeepsMasterTargetsVisible() throws Exception {
        String originalFontScale = originalFontScale();
        shell("settings put system font_scale 2.0");
        try (ActivityScenario<SettingsActivity> scenario = ActivityScenario.launch(SettingsActivity.class)) {
            scenario.onActivity(activity -> {
                View root = activity.findViewById(R.id.settings_root);
                int[] ids = {R.id.settings_display, R.id.settings_controls_master,
                        R.id.settings_audio_master, R.id.settings_game_language, R.id.settings_about};
                Rect rootRect = bounds(root);
                for (int id : ids) {
                    View target = activity.findViewById(id);
                    assertTrue(target.getHeight() >= Math.round(48 * target.getResources().getDisplayMetrics().density));
                    assertTrue(Rect.intersects(rootRect, bounds(target)));
                }
            });
        } finally {
            shell("settings put system font_scale " + originalFontScale);
        }
    }

    @Test public void licensesSelectReturnAndReadFailureRetry() {
        Intent intent = new Intent(ApplicationProvider.getApplicationContext(), LicensesActivity.class)
                .putExtra(LicensesActivity.EXTRA_FORCE_READ_ERROR, true);
        try (ActivityScenario<LicensesActivity> scenario = ActivityScenario.launch(intent)) {
            onView(withId(R.id.license_error)).check(matches(isDisplayed()));
            scenario.onActivity(activity -> activity.findViewById(R.id.license_retry).performClick());
            onView(withId(R.id.license_text)).check(matches(isDisplayed()));
            onView(withId(R.id.license_copy_link)).check(matches(isDisplayed()));
        }
    }

    private static Rect bounds(View view) {
        int[] p = new int[2]; view.getLocationOnScreen(p);
        return new Rect(p[0], p[1], p[0] + view.getWidth(), p[1] + view.getHeight());
    }

    private static void assertSection(SettingsActivity activity, int buttonId, String preferenceKey) {
        activity.findViewById(buttonId).performClick();
        activity.getSupportFragmentManager().executePendingTransactions();
        androidx.fragment.app.Fragment visible = visibleFragment(activity);
        org.junit.Assert.assertTrue(visible instanceof com.flynes.emu.settings.SettingsFragment);
        com.flynes.emu.settings.SettingsFragment fragment =
                (com.flynes.emu.settings.SettingsFragment) visible;
        org.junit.Assert.assertNotNull(fragment.findPreference(preferenceKey));
    }

    private static void assertDisplaySection(SettingsActivity activity) {
        activity.findViewById(R.id.settings_display).performClick();
        activity.getSupportFragmentManager().executePendingTransactions();
        androidx.fragment.app.Fragment visible = visibleFragment(activity);
        org.junit.Assert.assertTrue(visible instanceof
                com.flynes.emu.settings.DisplaySettingsFragment);
        org.junit.Assert.assertNotNull(visible.requireView().findViewById(
                R.id.video_preset_balanced));
    }

    private static androidx.fragment.app.Fragment visibleFragment(SettingsActivity activity) {
        for (androidx.fragment.app.Fragment fragment : activity.getSupportFragmentManager().getFragments()) {
            if (!fragment.isHidden()) return fragment;
        }
        throw new AssertionError("no visible settings fragment");
    }

    private static String originalFontScale() {
        String scale = com.flynes.emu.test.UiShell.run("settings get system font_scale").trim();
        return scale.isEmpty() ? "1.0" : scale;
    }

    private static void shell(String command) {
        com.flynes.emu.test.UiShell.run(command);
    }
}
