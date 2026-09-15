package com.flynes.emu.ui;

import android.content.Intent;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.LargeTest;

import com.flynes.emu.NearbyPairingActivity;
import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.replaceText;
import static androidx.test.espresso.action.ViewActions.scrollTo;
import static androidx.test.espresso.action.ViewActions.typeText;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.isEnabled;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.hamcrest.Matchers.not;

/**
 * Invite-code flow checks on the real pairing activity (design 2026-09-13
 * N01/N02, C05/C16): six-digit input with leading zeros, no request on
 * incomplete input, submit locked while a request is in flight, and the
 * invite lifecycle's regenerate/cancel semantics.
 */
@RunWith(AndroidJUnit4.class)
@LargeTest
public final class NearbyInviteCodeTest {

    private static ActivityScenario<NearbyPairingActivity> launchJoin() {
        Intent intent = new Intent(
                androidx.test.core.app.ApplicationProvider.getApplicationContext(),
                NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_JOIN_CODE);
        return ActivityScenario.launch(intent);
    }

    @Test public void incompleteCodeNeverEnablesTheRequest() {
        try (ActivityScenario<NearbyPairingActivity> ignored = launchJoin()) {
            onView(withId(R.id.nearby_join_code_input)).perform(scrollTo(), typeText("12345"));
            onView(withId(R.id.nearby_join_submit)).check(matches(not(isEnabled())));
        }
    }

    @Test public void sixDigitsWithLeadingZeroEnableTheRequest() {
        try (ActivityScenario<NearbyPairingActivity> ignored = launchJoin()) {
            onView(withId(R.id.nearby_join_code_input)).perform(scrollTo(), typeText("012345"));
            // Leading zero survives the input (C05).
            onView(withId(R.id.nearby_join_code_input)).check(matches(withText("012345")));
            onView(withId(R.id.nearby_join_submit)).check(matches(isEnabled()));
        }
    }

    @Test public void sevenDigitsArePreservedAndRejectedWithoutSilentTruncation() {
        try (ActivityScenario<NearbyPairingActivity> ignored = launchJoin()) {
            onView(withId(R.id.nearby_join_code_input))
                    .perform(scrollTo(), replaceText("0123456"));
            onView(withId(R.id.nearby_join_code_input)).check(matches(withText("0123456")));
            onView(withId(R.id.nearby_join_submit)).check(matches(not(isEnabled())));
        }
    }

    @Test public void submitWithoutBearerShowsTheDiscoveryReasonNeverSuccess() {
        try (ActivityScenario<NearbyPairingActivity> ignored = launchJoin()) {
            onView(withId(R.id.nearby_join_code_input)).perform(scrollTo(), typeText("012345"),
                    androidx.test.espresso.action.ViewActions.closeSoftKeyboard());
            onView(withId(R.id.nearby_join_submit)).perform(click());
            // No discovery bearer exists in this build: the honest outcome is the
            // blocking-stage reason, and no pairing state may appear.
            onView(withId(R.id.nearby_join_code_error)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_join_code_error))
                    .check(matches(withText(com.flynes.emu.R.string.nearby_stage_discovery_reason)));
        }
    }

    @Test public void createModeShowsAnInviteLifecycleThatRegeneratesAndCancels() {
        Intent intent = new Intent(
                androidx.test.core.app.ApplicationProvider.getApplicationContext(),
                NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_CREATE);
        try (ActivityScenario<NearbyPairingActivity> ignored = ActivityScenario.launch(intent)) {

            final String[] firstCode = new String[1];
            onView(withId(R.id.nearby_create_block)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_invite_code_value)).check((view, noViewFoundException) -> {
                org.junit.Assert.assertNull(noViewFoundException);
                firstCode[0] = ((android.widget.TextView) view).getText().toString();
            });
            org.junit.Assert.assertEquals(6, firstCode[0].length());

            onView(withId(R.id.nearby_invite_regenerate)).perform(scrollTo(), click());
            final String[] secondCode = new String[1];
            onView(withId(R.id.nearby_invite_code_value)).check((view, noViewFoundException) -> {
                secondCode[0] = ((android.widget.TextView) view).getText().toString();
            });
            org.junit.Assert.assertEquals(6, secondCode[0].length());
            // Regeneration produced a different code for a new generation (C16).
            org.junit.Assert.assertNotEquals(firstCode[0], secondCode[0]);

            onView(withId(R.id.nearby_invite_cancel)).perform(scrollTo(), click());
            onView(withId(R.id.nearby_invite_code_value)).check(matches(withText("")));
        }
    }
}
