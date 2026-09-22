package com.flynes.emu.ui;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.*;
import static org.junit.Assert.*;

import android.widget.LinearLayout;
import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import com.flynes.emu.FlyNesApplication;
import com.flynes.emu.NearbyLobbyActivity;
import com.flynes.emu.NearbySessionOwner;
import com.flynes.emu.R;
import org.junit.Test;
import org.junit.runner.RunWith;

/** Fixed landscape lobby: three summary cards, paged details, guarded confirmation. */
@RunWith(AndroidJUnit4.class)
public final class NearbyLobbyTest {
    @Test public void firstScreenIsGameHostSeatAndDetailsArePaged() {
        try (ActivityScenario<NearbyLobbyActivity> scenario =
                     ActivityScenario.launch(NearbyLobbyActivity.class)) {
            scenario.onActivity(activity -> {
                LinearLayout rows = activity.findViewById(R.id.nearby_lobby_rows);
                assertEquals(NearbyLobbyActivity.fieldCount(), rows.getChildCount());
                assertEquals(LinearLayout.HORIZONTAL, rows.getOrientation());
            });
            for (int id : new int[]{R.id.nearby_lobby_row_rom_identity,
                    R.id.nearby_lobby_row_network_owner, R.id.nearby_lobby_row_seat}) {
                onView(withId(id)).check(matches(isDisplayed()));
            }
            onView(withId(R.id.nearby_lobby_details)).perform(click());
            onView(withText(string(R.string.nearby_lobby_friend_name) + "  1/11"))
                    .check(matches(isDisplayed()));
            onView(withText(R.string.nearby_details_next)).perform(click());
            onView(withText(string(R.string.nearby_lobby_identity_fingerprint) + "  2/11"))
                    .check(matches(isDisplayed()));
            onView(withText(R.string.nearby_details_previous)).perform(click());
            onView(withText(string(R.string.nearby_lobby_friend_name) + "  1/11"))
                    .check(matches(isDisplayed()));
            onView(withText(R.string.nearby_action_cancel)).perform(click());
            onView(withId(R.id.nearby_lobby_confirm)).check(matches(isDisplayed()));
        }
    }

    @Test public void unsupportedCardsHaveAccessibleStatus() {
        try (ActivityScenario<NearbyLobbyActivity> ignored =
                     ActivityScenario.launch(NearbyLobbyActivity.class)) {
            onView(withId(R.id.nearby_lobby_row_seat)).check(matches(withContentDescription(
                    string(R.string.nearby_lobby_seat) + ", " + string(R.string.nearby_not_supported))));
            onView(withId(R.id.nearby_lobby_row_rom_identity)).check(matches(withContentDescription(
                    string(R.string.nearby_lobby_rom_identity) + ", " + string(R.string.nearby_not_supported))));
        }
    }

    @Test public void unsupportedConfirmDoesNotConfirmOrStartGame() {
        try (ActivityScenario<NearbyLobbyActivity> scenario =
                     ActivityScenario.launch(NearbyLobbyActivity.class)) {
            onView(withId(R.id.nearby_lobby_confirm)).check(matches(isDisplayed())).perform(click());
            scenario.onActivity(activity -> {
                NearbySessionOwner.Snapshot snap = ((FlyNesApplication) activity.getApplication())
                        .nearbySessionOwner().snapshot();
                assertEquals(0, snap.pendingConfigLocalConfirmed);
                assertEquals(0, snap.pendingConfigPeerConfirmed);
                assertEquals(NearbySessionOwner.GAME_NOT_STARTED, snap.gameState);
                assertEquals(snap.linkState, activity.boundLinkState());
            });
        }
    }

    private static String string(int id) {
        return ApplicationProvider.getApplicationContext().getString(id);
    }
}
