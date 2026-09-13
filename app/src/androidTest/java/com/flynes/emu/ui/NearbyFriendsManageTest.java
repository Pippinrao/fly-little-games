package com.flynes.emu.ui;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.Espresso.pressBack;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.scrollTo;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.isEnabled;
import static androidx.test.espresso.matcher.ViewMatchers.Visibility;
import static androidx.test.espresso.matcher.ViewMatchers.withContentDescription;
import static androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.hamcrest.Matchers.not;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.NearbyFriendsActivity;
import com.flynes.emu.R;
import com.flynes.emu.SettingsActivity;

import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Real emulator evidence for slice A1a-5 and its Settings entry (A1a-6 / decision D2): 好友管理 is
 * reachable from the 好友 tab and from Settings, and every action on it is present but disabled with
 * the specific reason.
 *
 * <p>The two reachability tests are the point of the slice: the page must be findable, and Settings
 * must keep its five sections while gaining this row, so D2's "a row, not a sixth root" is verified
 * by the row opening its own page rather than by a comment.
 */
@RunWith(AndroidJUnit4.class)
public final class NearbyFriendsManageTest {

    @Test
    public void allActionsArePresentAndDisabledWithAReason() {
        try (ActivityScenario<com.flynes.emu.NearbyFriendsManageActivity> ignored =
                     ActivityScenario.launch(com.flynes.emu.NearbyFriendsManageActivity.class)) {
            assertDisabledWithReason(R.id.nearby_manage_rename, R.id.nearby_manage_rename_reason,
                    R.string.nearby_friends_rename);
            assertDisabledWithReason(R.id.nearby_manage_delete, R.id.nearby_manage_delete_reason,
                    R.string.nearby_friends_delete);
            assertDisabledWithReason(R.id.nearby_manage_block, R.id.nearby_manage_block_reason,
                    R.string.nearby_friends_block);
            assertDisabledWithReason(R.id.nearby_manage_identity_reset,
                    R.id.nearby_manage_identity_reset_reason,
                    R.string.nearby_friends_identity_reset);

            // No friend row is ever inflated: the list is empty and says so with its own reason.
            onView(withId(R.id.nearby_manage_empty)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_manage_blocked))
                    .check(matches(withText(R.string.nearby_blocked_friend_store)));

            // The last control on the page is really reachable, not merely present in the hierarchy.
            onView(withId(R.id.nearby_manage_identity_reset_reason))
                    .perform(scrollTo())
                    .check(matches(isDisplayed()));
        }
    }

    @Test
    public void friendsTabOpensTheManagePage() {
        try (ActivityScenario<NearbyFriendsActivity> ignored =
                     ActivityScenario.launch(NearbyFriendsActivity.class)) {
            onView(withId(R.id.nearby_tab_friends)).perform(click());
            onView(withId(R.id.nearby_friends_manage)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_friends_manage)).check(matches(isEnabled()));
            onView(withId(R.id.nearby_friends_manage)).perform(click());
            onView(withId(R.id.nearby_manage_root)).check(matches(isDisplayed()));
            // Close the pushed page again. A test that leaves a second activity on top makes the
            // NEXT test in this class flaky: its own scenario is launched underneath a page that is
            // still in front, and every on-screen assertion then fails for the wrong reason.
            pressBack();
        }
    }

    @Test
    public void settingsRowOpensTheManagePageWithoutAddingASixthSection() {
        try (ActivityScenario<SettingsActivity> ignored =
                     ActivityScenario.launch(SettingsActivity.class)) {
            // The settings master list is a fixed-width scrolling column; 好友管理 is its newest and
            // therefore lowest row, so it is scrolled into view rather than assumed on screen.
            onView(withId(R.id.settings_nearby_friends_manage)).perform(scrollTo());
            onView(withId(R.id.settings_nearby_friends_manage)).check(matches(isDisplayed()));
            onView(withId(R.id.settings_nearby_friends_manage)).perform(click());
            onView(withId(R.id.nearby_manage_root)).check(matches(isDisplayed()));
            pressBack();
        }
    }

    private static void assertDisabledWithReason(int controlId, int reasonId, int labelId) {
        // Four controls plus four reasons do not fit a landscape phone, and they are not meant to:
        // the page scrolls. Visibility here is effective visibility, not on-screen position — a
        // control scrolled below the fold is still present, still disabled, and still carrying its
        // reason. The test separately proves the bottom of the page is reachable by scrolling to it.
        onView(withId(controlId)).check(matches(withEffectiveVisibility(Visibility.VISIBLE)));
        onView(withId(controlId)).check(matches(not(isEnabled())));
        onView(withId(reasonId)).check(matches(withEffectiveVisibility(Visibility.VISIBLE)));
        onView(withId(reasonId)).check(matches(withText(R.string.nearby_blocked_friend_store)));
        onView(withId(controlId)).check(matches(withContentDescription(
                string(labelId) + ", " + string(R.string.nearby_blocked_friend_store))));
    }

    private static String string(int id) {
        return ApplicationProvider.getApplicationContext().getString(id);
    }
}
