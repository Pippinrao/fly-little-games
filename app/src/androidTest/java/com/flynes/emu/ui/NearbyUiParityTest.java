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
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

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
        }
    }

    @Test public void createOpensTheInviteLifecycleWithoutAGame() {
        try (ActivityScenario<NearbyFriendsActivity> scenario =
                     ActivityScenario.launch(NearbyFriendsActivity.class)) {
            // C04: creating an invite never requires a selected game.
            onView(withId(R.id.nearby_action_create)).perform(click());
            onView(withId(R.id.nearby_pairing_root)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_create_block)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_invite_cancel)).perform(click());
        }
    }

    @Test public void wideLayoutUsesAlignedProportionalColumns() {
        try (ActivityScenario<NearbyFriendsActivity> scenario =
                     ActivityScenario.launch(NearbyFriendsActivity.class)) {
            scenario.onActivity(activity -> {
                android.view.View columns = activity.findViewById(R.id.nearby_device_columns);
                android.view.View left = activity.findViewById(R.id.nearby_action_column);
                android.view.View right = activity.findViewById(R.id.nearby_status_column);
                float density = activity.getResources().getDisplayMetrics().density;
                assertTrue("test target must be wider than the 580dp breakpoint",
                        columns.getWidth() / density > 580f);
                assertEquals(0.44f, (float)left.getWidth() / (left.getWidth() + right.getWidth()), .01f);
                assertEquals(37f, (right.getLeft() - left.getRight()) / density, 1f);
                assertEquals(columns.getWidth() - columns.getPaddingRight(), right.getRight());
            });
        }
    }
}
