package com.flynes.emu;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.scrollTo;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.view.View;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.settings.SettingsRepository;
import com.flynes.emu.settings.SharedPreferencesSettingsStore;
import com.flynes.emu.video.quality.VideoQualityPreset;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class DisplayQualitySettingsTest {
    @Test public void presetCardsStatusLayersAndTouchTargetsAreVisible() {
        try (ActivityScenario<SettingsActivity> scenario =
                     ActivityScenario.launch(SettingsActivity.class)) {
            int[] ids = {R.id.video_preset_power_card, R.id.video_preset_balanced_card,
                    R.id.video_preset_extreme_card, R.id.video_preset_custom_card,
                    R.id.video_adaptive_protection};
            for (int id : ids) onView(withId(id)).perform(scrollTo()).check(matches(isDisplayed()));
            onView(withId(R.id.video_status_request)).perform(scrollTo()).check(matches(isDisplayed()));
            onView(withId(R.id.video_status_system)).perform(scrollTo()).check(matches(isDisplayed()));
            onView(withId(R.id.video_status_evidence)).perform(scrollTo()).check(matches(isDisplayed()));
            scenario.onActivity(activity -> {
                float density = activity.getResources().getDisplayMetrics().density;
                for (int id : ids) {
                    View target = activity.findViewById(id);
                    assertTrue(target.getHeight() >= Math.round(48f * density));
                }
            });
        }
    }

    @Test public void customCardRevealsAxesAndPersistsOneAtomicPreset() {
        try (ActivityScenario<SettingsActivity> scenario =
                     ActivityScenario.launch(SettingsActivity.class)) {
            onView(withId(R.id.video_preset_custom_card)).perform(scrollTo(), click());
            onView(withId(R.id.video_custom_controls)).perform(scrollTo())
                    .check(matches(isDisplayed()));
            scenario.onActivity(activity -> assertEquals(VideoQualityPreset.CUSTOM,
                    new SettingsRepository(new SharedPreferencesSettingsStore(activity))
                            .load().videoPreferences().preset()));
        }
    }

    @Test public void lockedExtremeRemainsFocusableAndExplainsWhy() {
        try (ActivityScenario<SettingsActivity> ignored =
                     ActivityScenario.launch(SettingsActivity.class)) {
            onView(withId(R.id.video_preset_extreme_card)).perform(scrollTo(), click());
            onView(withText(R.string.video_extreme_locked_title)).check(matches(isDisplayed()));
            onView(withText(R.string.video_extreme_locked_message)).check(matches(isDisplayed()));
        }
    }
}
