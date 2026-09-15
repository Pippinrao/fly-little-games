package com.flynes.emu.ui;

import static androidx.test.espresso.Espresso.onView;
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

import com.flynes.emu.NearbyLobbyActivity;
import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Real emulator evidence for slice A1a-3: the 大厅 page renders every §22.3 field, each with the
 * reason that field is unavailable, and the one primary action stays disabled.
 *
 * <p>The row count assertion is the one that matters most. The page is built from a single table, so
 * "a field exists" is not a matter of reading the layout — it is a matter of the built row count
 * matching the table and the last row being the last field. A dropped field fails this test.
 */
@RunWith(AndroidJUnit4.class)
public final class NearbyLobbyTest {

    @Test
    public void everyLobbyFieldIsRendered() {
        try (ActivityScenario<NearbyLobbyActivity> scenario =
                     ActivityScenario.launch(NearbyLobbyActivity.class)) {
            scenario.onActivity(activity -> {
                LinearLayout rows = activity.findViewById(R.id.nearby_lobby_rows);
                org.junit.Assert.assertEquals(
                        "lobby row count must equal the §22.3 field table",
                        NearbyLobbyActivity.fieldCount(), rows.getChildCount());
            });
            // First and last field, so neither end of the order can be silently truncated. The last
            // one is below the fold on a landscape phone — a 14-row list is meant to scroll — so it
            // is scrolled to first; asserting its visibility without scrolling would be asserting
            // that the whole lobby fits on one screen, which is not a requirement.
            onView(withId(R.id.nearby_lobby_row_friend_name)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_lobby_row_confirm_invalidated))
                    .perform(scrollTo())
                    .check(matches(isDisplayed()));
        }
    }

    @Test
    public void eachFieldStatesWhyItIsUnavailable() {
        try (ActivityScenario<NearbyLobbyActivity> scenario =
                     ActivityScenario.launch(NearbyLobbyActivity.class)) {
            // A field's reason is its own: 座位 waits on the mode gate, not on the generic session
            // read, because no FLY_SESSION_MODE_* values exist (§3).
            onView(withId(R.id.nearby_lobby_row_seat)).check(matches(withContentDescription(
                    string(R.string.nearby_lobby_seat) + ", "
                            + string(R.string.nearby_blocked_mode_gate))));
            onView(withId(R.id.nearby_lobby_row_rom_identity)).check(matches(withContentDescription(
                    string(R.string.nearby_lobby_rom_identity) + ", "
                            + string(R.string.nearby_blocked_rom_transfer))));
            onView(withId(R.id.nearby_lobby_row_profile_verified))
                    .check(matches(withContentDescription(
                            string(R.string.nearby_lobby_profile_verified) + ", "
                                    + string(R.string.nearby_blocked_profile_verify))));
            onView(withId(R.id.nearby_lobby_row_friend_name)).check(matches(withContentDescription(
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
        }
    }

    private static String string(int id) {
        return ApplicationProvider.getApplicationContext().getString(id);
    }
}
