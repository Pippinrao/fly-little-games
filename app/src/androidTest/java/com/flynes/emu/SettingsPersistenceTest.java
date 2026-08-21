package com.flynes.emu;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.matcher.ViewMatchers.hasDescendant;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.junit.Assert.assertEquals;

import android.content.Context;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.espresso.contrib.RecyclerViewActions;

import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.AspectMode;
import com.flynes.emu.settings.SharedPreferencesSettingsStore;
import com.flynes.emu.settings.SettingsRepository;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class SettingsPersistenceTest {
    @Test
    public void hapticAndAspectPersistAcrossRecreation() {
        Context context = ApplicationProvider.getApplicationContext();
        context.getSharedPreferences(SettingsRepository.PREFERENCES_NAME, 0)
                .edit().clear().commit();

        try (ActivityScenario<SettingsActivity> scenario =
                     ActivityScenario.launch(SettingsActivity.class)) {
            onView(withText(R.string.aspect_mode)).perform(click());
            onView(withText(R.string.aspect_square_pixels)).perform(click());
            onView(withId(androidx.preference.R.id.recycler_view)).perform(
                    RecyclerViewActions.scrollTo(hasDescendant(withText(R.string.haptic_level))));
            onView(withText(R.string.haptic_level)).perform(click());
            onView(withText(R.string.haptic_off)).perform(click());
            scenario.recreate();
        }

        AppSettings saved = new SettingsRepository(
                new SharedPreferencesSettingsStore(context)).load();
        assertEquals(AspectMode.SQUARE_PIXELS, saved.aspectMode());
        assertEquals(HapticLevel.OFF, saved.hapticLevel());
    }
}
