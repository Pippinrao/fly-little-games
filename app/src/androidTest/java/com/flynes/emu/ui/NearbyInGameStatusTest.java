package com.flynes.emu.ui;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import android.view.View;
import android.view.ViewGroup;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.MainActivity;
import com.flynes.emu.NearbyInGameStatus;
import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Real emulator evidence for slice A1a-4, and specifically for decision D7: during local
 * single-player the run surface carries <em>no</em> nearby status at all — not a blocked row, not a
 * placeholder banner.
 *
 * <p>This is the assertion that keeps the nearby feature out of the one screen where the user is
 * playing a game. It is written as an absence check over {@link NearbyInGameStatus#allRowIds()} so
 * that a row added later cannot escape it, and it inspects the real drawer the activity builds
 * rather than a resource.
 *
 * <p>The action-demanding banner (D6) and the suspended-state rows are the same code path; they are
 * unreachable while no session exists, so this test also pins the guard that makes them unreachable
 * instead of leaving D7 to a comment.
 */
@RunWith(AndroidJUnit4.class)
public final class NearbyInGameStatusTest {

    @Test
    public void theGuardKeepsEveryNearbyRowOutWithoutASession() {
        // The table must stay non-empty, otherwise the absence assertions below would pass while
        // proving nothing at all.
        assertTrue("the nearby in-game table must declare rows",
                NearbyInGameStatus.allRowIds().length >= 13);
        assertEquals(0, NearbyInGameStatus.drawerRowIds(false).length);
        assertEquals(0, NearbyInGameStatus.bannerRowIds(false).length);
        assertEquals(NearbyInGameStatus.drawerRowIds(true).length,
                NearbyInGameStatus.drawerLabelIds().length);
        assertEquals(NearbyInGameStatus.bannerRowIds(true).length,
                NearbyInGameStatus.bannerValueIds().length);
    }

    @Test
    public void localSinglePlayerShowsNoNearbyStatusOnTheRunSurfaceOrInTheDrawer() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                // No session can exist yet (the Android binding has no fly_session surface), so the
                // pinned banner must not be attached to the run surface.
                assertNull("D7: no nearby banner without a session",
                        activity.findViewById(R.id.nearby_ingame_banner));
                assertNoNearbyRow(activity.getWindow().getDecorView(),
                        "run surface before opening the pause drawer");

                // The drawer is built synchronously by this click, so its contents can be inspected
                // in the same main-thread callback. Its own visibility is deliberately not asserted:
                // it slides in over 220 ms, and this test is about which rows exist, not about the
                // animation. Asserting on-screen visibility here would make the result depend on
                // whether the emulator has animations enabled.
                activity.findViewById(R.id.pause_button).performClick();
                assertNotNull("the pause drawer must exist after the pause button is clicked",
                        activity.findViewById(R.id.pause_drawer));
                assertNoNearbyRow(activity.findViewById(R.id.pause_drawer), "pause drawer");
            });
        }
    }

    private static void assertNoNearbyRow(View root, String where) {
        int[] ids = NearbyInGameStatus.allRowIds();
        for (int id : ids) {
            View found = root.findViewById(id);
            assertNull("D7 violated: nearby row " + where + " in local single-player", found);
        }
        if (root instanceof ViewGroup) {
            ViewGroup group = (ViewGroup) root;
            for (int i = 0; i < group.getChildCount(); i++) {
                assertNoNearbyRow(group.getChildAt(i), where);
            }
        }
    }
}
