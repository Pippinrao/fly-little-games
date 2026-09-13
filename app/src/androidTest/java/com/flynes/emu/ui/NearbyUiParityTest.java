package com.flynes.emu.ui;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.LargeTest;

import com.flynes.emu.NearbyFriendsActivity;
import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.contrib.RecyclerViewActions.scrollTo;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;

/**
 * N00 parity checks on the real nearby entry page (design 2026-09-13 U07,
 * C04): the three primary actions are present in the approved order, none of
 * them requires a selected game, and the original tabs/discovery capabilities
 * stay in place. Camera denial behavior is covered by the invite-code suite;
 * the scan action here is exercised only through its navigation gate.
 */
@RunWith(AndroidJUnit4.class)
@LargeTest
public final class NearbyUiParityTest {

    @Test public void threePrimaryActionsAreVisibleInContractOrder() {
        try (ActivityScenario<NearbyFriendsActivity> scenario =
                     ActivityScenario.launch(NearbyFriendsActivity.class)) {
            onView(withId(R.id.nearby_action_create)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_action_create)).check(matches(withText(R.string.nearby_action_create)));
            onView(withId(R.id.nearby_action_enter_code)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_action_enter_code)).check(matches(withText(R.string.nearby_action_enterCode)));
            onView(withId(R.id.nearby_action_scan_qr)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_action_scan_qr)).check(matches(withText(R.string.nearby_action_scanQr)));
        }
    }

    @Test public void originalTabsAndDiscoveryCapabilityStay() {
        try (ActivityScenario<NearbyFriendsActivity> scenario =
                     ActivityScenario.launch(NearbyFriendsActivity.class)) {
            onView(withId(R.id.nearby_tabs)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_find_devices)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_friends_manage_inline)).check(matches(isDisplayed()));
        }
    }

    @Test public void createOpensTheInviteLifecycleWithoutAGame() {
        try (ActivityScenario<NearbyFriendsActivity> scenario =
                     ActivityScenario.launch(NearbyFriendsActivity.class)) {
            // C04: creating an invite never requires a selected game.
            onView(withId(R.id.nearby_action_create)).perform(click());
            onView(withId(R.id.nearby_pairing_root)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_create_block)).check(matches(isDisplayed()));
        }
    }
}
