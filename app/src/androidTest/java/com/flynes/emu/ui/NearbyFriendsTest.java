package com.flynes.emu.ui;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.Espresso.pressBack;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isChecked;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.isEnabled;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.hamcrest.Matchers.not;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.HomeActivity;
import com.flynes.emu.NearbyFriendsActivity;
import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

/** Real entry navigation, empty tabs, and unsupported-action feedback without scrolling. */
@RunWith(AndroidJUnit4.class)
public final class NearbyFriendsTest {

    @Test
    public void gameCenterShowsTheNearbyEntry() {
        try (ActivityScenario<HomeActivity> ignored = ActivityScenario.launch(HomeActivity.class)) {
            onView(withId(R.id.open_nearby)).check(matches(isDisplayed()));
            onView(withId(R.id.open_nearby)).check(matches(withText(R.string.nearby_open)));
            onView(withId(R.id.open_nearby)).check(matches(isEnabled()));
        }
    }

    @Test
    public void nearbyEntryOpensTheNearbyPage() {
        try (ActivityScenario<HomeActivity> ignored = ActivityScenario.launch(HomeActivity.class)) {
            onView(withId(R.id.open_nearby)).perform(click());
            // The page is now on screen; this is the only proof that the entry's wiring works.
            onView(withId(R.id.nearby_root)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_toolbar)).check(matches(isDisplayed()));
            pressBack();
        }
    }

    @Test
    public void pageLandsOnDevicesTabWhileNoFriendIsSaved() {
        try (ActivityScenario<NearbyFriendsActivity> ignored =
                     ActivityScenario.launch(NearbyFriendsActivity.class)) {
            // §4 initial-tab rule: no saved friend -> 附近设备, the page's only actionable side.
            onView(withId(R.id.nearby_tab_devices)).check(matches(isChecked()));
            onView(withId(R.id.nearby_devices_panel)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_friends_panel)).check(matches(not(isDisplayed())));
        }
    }

    @Test
    public void discoveryControlsRespondWithoutLeavingThePage() {
        try (ActivityScenario<NearbyFriendsActivity> ignored =
                     ActivityScenario.launch(NearbyFriendsActivity.class)) {
            onView(withId(R.id.nearby_find_devices)).perform(click());
            onView(withId(R.id.nearby_find_devices)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_find_devices)).check(matches(isEnabled()));
            onView(withId(R.id.nearby_find_devices_reason)).perform(click());
            onView(withId(R.id.nearby_find_devices_reason)).check(matches(isDisplayed()));
        }
    }

    @Test
    public void friendsTabShowsOnlyTheEmptyStateAndItsBlockedReason() {
        try (ActivityScenario<NearbyFriendsActivity> ignored =
                     ActivityScenario.launch(NearbyFriendsActivity.class)) {
            onView(withId(R.id.nearby_tab_friends)).perform(click());
            onView(withId(R.id.nearby_friends_panel)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_friends_empty)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_friends_blocked))
                    .check(matches(withText(R.string.nearby_not_supported)));
            onView(withId(R.id.nearby_devices_panel)).check(matches(not(isDisplayed())));
        }
    }
}
