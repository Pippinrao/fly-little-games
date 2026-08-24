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
import com.flynes.emu.settings.SharedPreferencesSettingsStore;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class HapticSettingsPersistenceTest {
    @Test
    public void hapticLevelAndDistinctABPersistAcrossRecreation() {
        ApplicationProvider.getApplicationContext()
                .getSharedPreferences(SettingsActivity.PREFS, 0).edit().clear().commit();
        try (ActivityScenario<SettingsActivity> scenario =
                     ActivityScenario.launch(SettingsActivity.class)) {
            onView(withId(R.id.settings_controls_master)).perform(click());
            onView(allOf(withId(androidx.preference.R.id.recycler_view), isDisplayed())).perform(
                    RecyclerViewActions.scrollTo(hasDescendant(withText(R.string.haptic_level))));
            onView(withText(R.string.haptic_level)).perform(click());
            onView(withText(R.string.haptic_off)).perform(click());
            onView(allOf(withId(androidx.preference.R.id.recycler_view), isDisplayed())).perform(
                    RecyclerViewActions.scrollTo(hasDescendant(
                            withText(R.string.distinct_ab_haptics))));
            onView(withText(R.string.distinct_ab_haptics)).perform(click());
            scenario.recreate();
        }
        AppSettings settings = new SettingsRepository(new SharedPreferencesSettingsStore(
                ApplicationProvider.getApplicationContext())).load();
        assertEquals(HapticLevel.OFF, settings.hapticLevel());
        assertFalse(settings.distinctABHaptics());
    }
}
