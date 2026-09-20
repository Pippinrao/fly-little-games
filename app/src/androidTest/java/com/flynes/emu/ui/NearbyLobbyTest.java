package com.flynes.emu.ui;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.scrollTo;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.isEnabled;
import static androidx.test.espresso.matcher.ViewMatchers.withContentDescription;
import static androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static androidx.test.espresso.matcher.ViewMatchers.Visibility;
import static org.hamcrest.Matchers.not;

import android.widget.LinearLayout;
import android.widget.ScrollView;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.FlyNesApplication;
import com.flynes.emu.NearbyLobbyActivity;
import com.flynes.emu.NearbySessionOwner;
import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Real emulator evidence for N09: the 大厅 first screen is game / host / seat plus
 * a disabled confirm. Technical §22.3 fields stay collapsed until details is opened.
 */
@RunWith(AndroidJUnit4.class)
public final class NearbyLobbyTest {

    @Test
    public void firstScreenIsGameHostSeatAndKeepsTechInDetails() {
        try (ActivityScenario<NearbyLobbyActivity> scenario =
                     ActivityScenario.launch(NearbyLobbyActivity.class)) {
            scenario.onActivity(activity -> {
                LinearLayout rows = activity.findViewById(R.id.nearby_lobby_rows);
                org.junit.Assert.assertEquals(
                        "first screen is three fields plus details control plus collapsed details",
                        NearbyLobbyActivity.fieldCount() + 2, rows.getChildCount());
                org.junit.Assert.assertEquals(android.view.View.GONE,
                        activity.findViewById(R.id.nearby_lobby_details_rows).getVisibility());
            });
            onView(withId(R.id.nearby_lobby_row_rom_identity)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_lobby_row_network_owner)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_lobby_row_seat)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_lobby_row_identity_fingerprint))
                    .check(matches(not(isDisplayed())));
            onView(withId(R.id.nearby_lobby_row_friend_name))
                    .check(matches(not(isDisplayed())));
            onView(withId(R.id.nearby_lobby_details)).perform(scrollTo(), click());
            onView(withId(R.id.nearby_lobby_row_identity_fingerprint))
                    .perform(scrollTo())
                    .check(matches(isDisplayed()));
            onView(withId(R.id.nearby_lobby_row_friend_name))
                    .perform(scrollTo())
                    .check(matches(isDisplayed()));
        }
    }

    @Test
    public void eachFirstScreenFieldStatesWhyItIsUnavailable() {
        try (ActivityScenario<NearbyLobbyActivity> scenario =
                     ActivityScenario.launch(NearbyLobbyActivity.class)) {
            onView(withId(R.id.nearby_lobby_row_seat)).check(matches(withContentDescription(
                    string(R.string.nearby_lobby_seat) + ", "
                            + string(R.string.nearby_blocked_mode_gate))));
            onView(withId(R.id.nearby_lobby_row_rom_identity)).check(matches(withContentDescription(
                    string(R.string.nearby_lobby_rom_identity) + ", "
                            + string(R.string.nearby_blocked_rom_transfer))));
            onView(withId(R.id.nearby_lobby_details)).perform(scrollTo(), click());
            onView(withId(R.id.nearby_lobby_row_profile_verified))
                    .perform(scrollTo())
                    .check(matches(withContentDescription(
                            string(R.string.nearby_lobby_profile_verified) + ", "
                                    + string(R.string.nearby_blocked_profile_verify))));
            onView(withId(R.id.nearby_lobby_row_friend_name))
                    .perform(scrollTo())
                    .check(matches(withContentDescription(
                            string(R.string.nearby_lobby_friend_name) + ", "
                                    + string(R.string.nearby_blocked_session_read))));
        }
    }

    @Test
    public void confirmIsOneDisabledPrimaryActionWithAReason() {
        try (ActivityScenario<NearbyLobbyActivity> scenario =
                     ActivityScenario.launch(NearbyLobbyActivity.class)) {
            // D8: one primary action per side, bound to the pending configuration. There is no
            // configuration to read yet, so it is disabled and says so rather than confirming.
            scenario.onActivity(activity -> {
                android.view.ViewParent parent = activity.findViewById(
                        R.id.nearby_lobby_confirm).getParent();
                while (parent != null) {
                    org.junit.Assert.assertFalse(
                            "confirm footer must remain outside scrolling content",
                            parent instanceof ScrollView);
                    parent = parent.getParent();
                }
            });
            onView(withId(R.id.nearby_lobby_confirm)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_lobby_confirm)).check(matches(not(isEnabled())));
            onView(withId(R.id.nearby_lobby_confirm_reason))
                    .check(matches(withEffectiveVisibility(Visibility.VISIBLE)));
            onView(withId(R.id.nearby_lobby_confirm_reason))
                    .check(matches(withText(R.string.nearby_blocked_session_read)));
            scenario.onActivity(activity -> {
                FlyNesApplication app = (FlyNesApplication) activity.getApplication();
                NearbySessionOwner.Snapshot snap = app.nearbySessionOwner().snapshot();
                org.junit.Assert.assertEquals(0, snap.pendingConfigLocalConfirmed);
                org.junit.Assert.assertEquals(0, snap.pendingConfigPeerConfirmed);
                org.junit.Assert.assertEquals(
                        "confirm disablement is bound to the process owner snapshot",
                        snap.linkState, activity.boundLinkState());
            });
        }
    }

    private static String string(int id) {
        return ApplicationProvider.getApplicationContext().getString(id);
    }
}
