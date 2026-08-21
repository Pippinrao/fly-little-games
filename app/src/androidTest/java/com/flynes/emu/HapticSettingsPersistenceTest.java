package com.flynes.emu;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isChecked;
import static androidx.test.espresso.matcher.ViewMatchers.isNotChecked;
import static androidx.test.espresso.matcher.ViewMatchers.withId;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

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
            onView(withId(R.id.haptic_off)).perform(click());
            onView(withId(R.id.haptic_distinct)).perform(click());
            scenario.recreate();
            onView(withId(R.id.haptic_off)).check(matches(isChecked()));
            onView(withId(R.id.haptic_distinct)).check(matches(isNotChecked()));
        }
    }
}
