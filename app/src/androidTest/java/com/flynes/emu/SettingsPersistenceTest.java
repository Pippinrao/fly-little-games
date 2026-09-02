package com.flynes.emu;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.scrollTo;
import static androidx.test.espresso.matcher.ViewMatchers.hasDescendant;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertArrayEquals;
import static org.hamcrest.Matchers.allOf;

import android.content.Context;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.espresso.contrib.RecyclerViewActions;

import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.input.DirectionControlMode;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.AspectMode;
import com.flynes.emu.settings.SharedPreferencesSettingsStore;
import com.flynes.emu.settings.SettingsRepository;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class SettingsPersistenceTest {
    @Test
    public void directionModesAreOrderedAndCleanDefaultThenFollowPersist() {
        Context context = ApplicationProvider.getApplicationContext();
        context.getSharedPreferences(SettingsRepository.PREFERENCES_NAME, 0)
                .edit().clear().commit();
        assertArrayEquals(new String[]{
                        context.getString(R.string.direction_fixed_joystick),
                        context.getString(R.string.direction_joystick),
                        context.getString(R.string.direction_dpad)},
                strings(context.getResources().getTextArray(
                        R.array.direction_control_entries)));
        assertArrayEquals(new String[]{"FIXED_JOYSTICK", "JOYSTICK", "DPAD"},
                strings(context.getResources().getTextArray(
                        R.array.direction_control_values)));

        try (ActivityScenario<SettingsActivity> scenario =
                     ActivityScenario.launch(SettingsActivity.class)) {
            onView(withId(R.id.settings_controls_master)).perform(click());
            scenario.onActivity(activity -> {
                com.flynes.emu.settings.SettingsFragment fragment =
                        (com.flynes.emu.settings.SettingsFragment) activity
                                .getSupportFragmentManager()
                                .findFragmentById(R.id.settings_content);
                androidx.preference.ListPreference preference =
                        fragment.findPreference("controls.direction_mode");
                assertEquals("FIXED_JOYSTICK", preference.getValue());
                assertEquals(context.getString(R.string.direction_fixed_joystick),
                        preference.getSummary());
            });
            onView(withText(R.string.direction_control)).perform(click());
            onView(withText(R.string.direction_joystick)).perform(click());
            scenario.recreate();
        }

        assertEquals(DirectionControlMode.JOYSTICK,
                new SettingsRepository(new SharedPreferencesSettingsStore(context))
                        .load().directionControlMode());
    }

    @Test
    public void hapticAndAspectPersistAcrossRecreation() {
        Context context = ApplicationProvider.getApplicationContext();
        context.getSharedPreferences(SettingsRepository.PREFERENCES_NAME, 0)
                .edit().clear().commit();

        try (ActivityScenario<SettingsActivity> scenario =
                     ActivityScenario.launch(SettingsActivity.class)) {
            // Aspect now belongs to the explicit Custom quality axes, not a legacy preference row.
            onView(withId(R.id.video_preset_custom_card)).perform(scrollTo(), click());
            onView(withId(R.id.video_aspect_spinner)).perform(scrollTo(), click());
            onView(withText(R.string.aspect_square_pixels)).perform(click());
            onView(withId(R.id.settings_controls_master)).perform(click());
            onView(withText(R.string.direction_control)).perform(click());
            onView(withText(R.string.direction_dpad)).perform(click());
            onView(allOf(withId(androidx.preference.R.id.recycler_view), isDisplayed())).perform(
                    RecyclerViewActions.scrollTo(hasDescendant(withText(R.string.haptic_level))));
            onView(withText(R.string.haptic_level)).perform(click());
            onView(withText(R.string.haptic_off)).perform(click());
            scenario.recreate();
        }

        AppSettings saved = new SettingsRepository(
                new SharedPreferencesSettingsStore(context)).load();
        assertEquals(AspectMode.SQUARE_PIXELS, saved.aspectMode());
        assertEquals(HapticLevel.OFF, saved.hapticLevel());
        assertEquals(DirectionControlMode.DPAD, saved.directionControlMode());
    }

    private static String[] strings(CharSequence[] values) {
        String[] result = new String[values.length];
        for (int index = 0; index < values.length; index++) {
            result[index] = values[index].toString();
        }
        return result;
    }
}
