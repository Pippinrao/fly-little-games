package com.flynes.emu.ui;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.LargeTest;

import com.flynes.emu.HomeActivity;
import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.isEnabled;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.hamcrest.Matchers.not;

/**
 * Nearby UI parity checks that run against the real home activity (design
 * 2026-09-13 U02, C01, C04). The entry text is the U02 contract text; the four
 * original categories stay in place; the two-player switch is an independent
 * control on the count row, never a fifth category.
 */
@RunWith(AndroidJUnit4.class)
@LargeTest
public final class HomeMultiplayerFilterTest {

    @Test public void entryShowsNearbyOpenWhenCleanlyDisconnected() {
        try (ActivityScenario<HomeActivity> scenario = ActivityScenario.launch(HomeActivity.class)) {
            onView(withId(R.id.open_nearby)).check(matches(withText(R.string.nearby_open)));
        }
    }

    @Test public void twoPlayerSwitchIsAnIndependentControlNotAFifthCategory() {
        try (ActivityScenario<HomeActivity> scenario = ActivityScenario.launch(HomeActivity.class)) {
            onView(withId(R.id.multiplayer_filter)).check(matches(isDisplayed()));
            onView(withId(R.id.multiplayer_filter)).check(matches(withText(R.string.nearby_filter_multiplayerOnly)));
            onView(withId(R.id.category_recent)).check(matches(isDisplayed()));
            onView(withId(R.id.category_favorites)).check(matches(isDisplayed()));
            onView(withId(R.id.category_all)).check(matches(isDisplayed()));
            onView(withId(R.id.category_builtin)).check(matches(isDisplayed()));
        }
    }

    @Test public void zeroResultKeepsBuiltinAndOffersTheContractAction() {
        // The filter persists per device (U04); start every run from the
        // default-off state so the case is hermetic.
        androidx.test.core.app.ApplicationProvider.getApplicationContext()
                .getSharedPreferences("game_center_ui", android.content.Context.MODE_PRIVATE)
                .edit().remove("multiplayerOnly").commit();
        try (ActivityScenario<HomeActivity> scenario = ActivityScenario.launch(HomeActivity.class)) {
            // The capability registry is truthfully empty (UNKNOWN everywhere),
            // so enabling the filter must empty the visible list and surface
            // the contract action (C03) while the category stays BUILTIN.
            onView(withId(R.id.category_builtin)).perform(androidx.test.espresso.action.ViewActions.click());
            onView(withId(R.id.multiplayer_filter)).perform(
                    androidx.test.espresso.action.ViewActions.click());
            onView(withId(R.id.multiplayer_filter)).check(matches(isChecked()));
            onView(withId(R.id.disable_multiplayer_filter)).check(matches(isDisplayed()));
            onView(withId(R.id.disable_multiplayer_filter))
                    .check(matches(withText(R.string.nearby_action_disableMultiplayerFilter)));
            // Closing the switch clears only the switch: BUILTIN content returns.
            onView(withId(R.id.disable_multiplayer_filter)).perform(
                    androidx.test.espresso.action.ViewActions.click());
            onView(withId(R.id.disable_multiplayer_filter)).check(matches(not(isDisplayed())));
            onView(withId(R.id.multiplayer_filter)).check(matches(not(isChecked())));
            onView(withId(R.id.category_builtin)).check(matches(isDisplayed()));
        }
    }

    private static org.hamcrest.Matcher<android.view.View> isChecked() {
        return new org.hamcrest.BaseMatcher<android.view.View>() {
            @Override public boolean matches(Object item) {
                return item instanceof com.google.android.material.switchmaterial.SwitchMaterial
                        && ((com.google.android.material.switchmaterial.SwitchMaterial) item).isChecked();
            }
            @Override public void describeTo(org.hamcrest.Description description) {
                description.appendText("switch is checked");
            }
        };
    }
}
