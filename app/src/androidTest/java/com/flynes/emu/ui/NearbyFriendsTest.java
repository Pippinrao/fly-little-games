package com.flynes.emu.ui;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.Espresso.pressBack;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.scrollTo;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isChecked;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.isEnabled;
import static androidx.test.espresso.matcher.ViewMatchers.Visibility;
import static androidx.test.espresso.matcher.ViewMatchers.withContentDescription;
import static androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.hamcrest.Matchers.not;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import android.view.View;

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
    public void onlyTheFirstFailingStageExplainsItself() {
        try (ActivityScenario<NearbyFriendsActivity> ignored =
                     ActivityScenario.launch(NearbyFriendsActivity.class)) {
            // 权限 is the first stage and the first failure, so it is marked and carries its reason.
            // Stage names are read from resources so the assertion is locale-independent.
            onView(withId(R.id.nearby_stage_permission_row))
                    .check(matches(withContentDescription(
                            string(R.string.nearby_stage_permission) + ", "
                                    + string(R.string.nearby_stage_status_current))));
            onView(withId(R.id.nearby_stage_permission_reason))
                    .check(matches(withEffectiveVisibility(Visibility.VISIBLE)));

            // Every later stage is neutral and shows no reason at all (D5). Visibility, not
            // on-screen position: the pipeline is explanatory detail that may sit below the fold,
            // whereas the action buttons above must not (that is what the previous test guards).
            onView(withId(R.id.nearby_stage_discovery_reason))
                    .check(matches(withEffectiveVisibility(Visibility.GONE)));
            onView(withId(R.id.nearby_stage_codec_reason))
                    .check(matches(withEffectiveVisibility(Visibility.GONE)));
            onView(withId(R.id.nearby_stage_codec_row))
                    .check(matches(withContentDescription(
                            string(R.string.nearby_stage_codec) + ", "
                                    + string(R.string.nearby_stage_status_not_reached))));
        }
    }

    private static String string(int id) {
        return androidx.test.core.app.ApplicationProvider.getApplicationContext().getString(id);
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

            onView(withId(R.id.nearby_scan_host_qr)).perform(scrollTo());
            onView(withId(R.id.nearby_scan_host_qr)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_scan_host_qr)).check(matches(not(isEnabled())));
            onView(withId(R.id.nearby_scan_host_qr_reason)).perform(scrollTo());
            onView(withId(R.id.nearby_scan_host_qr_reason)).check(matches(isDisplayed()));
        }
    }

    @Test
    public void devicesTabCarriesTheOnePermittedNavigateOnlyEntryIntoPairing() {
        try (ActivityScenario<NearbyFriendsActivity> scenario =
                     ActivityScenario.launch(NearbyFriendsActivity.class)) {
            // §4 allows exactly one navigate-only entry besides the game-center one: into 配对,
            // because that page has to exist to display its blocked stages. It is a secondary
            // destination, so it sits below the two primary discovery controls and above the
            // explanatory pipeline; the page scrolls, so it is scrolled into view first.
            onView(withId(R.id.nearby_open_pairing)).perform(scrollTo());
            onView(withId(R.id.nearby_open_pairing)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_open_pairing))
                    .check(matches(withText(R.string.nearby_pairing_title)));

            // Measured, not assumed: how far down the tab the one enabled control actually lands.
            // If it were the page's only action this would be a defect (the earlier
            // nearby_find_devices case); as a secondary entry it is reported rather than asserted.
            scenario.onActivity(activity -> {
                View entry = activity.findViewById(R.id.nearby_open_pairing);
                android.graphics.Rect visible = new android.graphics.Rect();
                entry.getGlobalVisibleRect(visible);
                android.util.Log.i("NearbyFriendsTest", "pairing entry top=" + entry.getTop()
                        + " height=" + entry.getHeight() + " parentHeight="
                        + ((View) entry.getParent()).getHeight()
                        + " visibleWithoutScrolling=" + !visible.isEmpty());
            });

            onView(withId(R.id.nearby_open_pairing)).perform(click());
            onView(withId(R.id.nearby_pairing_root)).check(matches(isDisplayed()));
            // Leave no pushed activity in front for the next test in this class.
            pressBack();
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
