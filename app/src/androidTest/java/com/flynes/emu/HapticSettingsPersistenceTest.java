package com.flynes.emu;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.matcher.ViewMatchers.hasDescendant;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.hamcrest.Matchers.allOf;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.espresso.contrib.RecyclerViewActions;

import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.SettingsRepository;
import com.flynes.emu.settings.SettingsAccess;
import com.flynes.emu.settings.SharedPreferencesSettingsStore;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class HapticSettingsPersistenceTest {
    @Test
    public void hapticLevelAndDistinctABPersistAcrossRecreation() {
        assertEquals(true, SettingsAccess.repository(
                ApplicationProvider.getApplicationContext()).save(AppSettings.defaults()));
        try (ActivityScenario<SettingsActivity> scenario =
                     ActivityScenario.launch(SettingsActivity.class)) {
            final String[] labels = new String[3];
            scenario.onActivity(activity -> {
                labels[0] = activity.getString(R.string.haptic_level);
                labels[1] = activity.getString(R.string.haptic_off);
                labels[2] = activity.getString(R.string.distinct_ab_haptics);
            });
            onView(withId(R.id.settings_controls_master)).perform(click());
            onView(allOf(withId(androidx.preference.R.id.recycler_view), isDisplayed())).perform(
                    RecyclerViewActions.scrollTo(hasDescendant(withText(labels[0]))));
            onView(withText(labels[0])).perform(click());
            onView(withText(labels[1])).perform(click());
            onView(allOf(withId(androidx.preference.R.id.recycler_view), isDisplayed())).perform(
                    RecyclerViewActions.scrollTo(hasDescendant(
                            withText(labels[2]))));
            onView(withText(labels[2])).perform(click());
            scenario.recreate();
        }
        AppSettings settings = new SettingsRepository(new SharedPreferencesSettingsStore(
                ApplicationProvider.getApplicationContext())).load();
        assertEquals(HapticLevel.OFF, settings.hapticLevel());
        assertFalse(settings.distinctABHaptics());
    }
}
