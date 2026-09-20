package com.flynes.emu.ui;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.Espresso.pressBack;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.scrollTo;
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

/**
 * Real emulator evidence for slice A1a-1: the 附近联机 entry exists on the game center, the entry
 * actually opens the page, the page lands on 附近设备 while no friend is saved, only the first
 * failing stage explains itself, and the discovery controls are present but disabled with a visible
 * reason.
 *
 * <p>This exists because compile-and-package evidence is not UI evidence. Nothing here asserts a
 * resource exists in a file; every assertion drives the real activity on a device.
 */
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
    public void discoveryControlsArePresentButDisabledWithAReason() {
        try (ActivityScenario<NearbyFriendsActivity> ignored =
                     ActivityScenario.launch(NearbyFriendsActivity.class)) {
            onView(withId(R.id.nearby_find_devices)).perform(scrollTo());
            onView(withId(R.id.nearby_find_devices)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_find_devices)).check(matches(not(isEnabled())));
            onView(withId(R.id.nearby_find_devices_reason)).perform(scrollTo());
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
                    .check(matches(withText(R.string.nearby_blocked_friend_store)));
            onView(withId(R.id.nearby_devices_panel)).check(matches(not(isDisplayed())));
        }
    }
}
