package com.flynes.emu.ui;

import android.content.Intent;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.LargeTest;

import com.flynes.emu.FlyNesApplication;
import com.flynes.emu.NearbyPairingActivity;
import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.replaceText;
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

    /** Process-scoped join fence; delta of two probes is 1 when nothing was submitted. */
    private static long consumeJoinAttemptFence() {
        FlyNesApplication app = (FlyNesApplication)
                androidx.test.core.app.ApplicationProvider.getApplicationContext();
        return app.nearbySession().nextJoinAttemptId();
    }

    private static long joinSendsBetween(long beforeFence, long afterFence) {
        return afterFence - beforeFence - 1L;
    }

    private static long snapshotJoinAttemptId() {
        FlyNesApplication app = (FlyNesApplication)
                androidx.test.core.app.ApplicationProvider.getApplicationContext();
        return app.nearbySession().snapshot()[2];
    }

    @Test public void emptyJoinCodeShowsSubmitDisabled() {
        try (ActivityScenario<NearbyPairingActivity> ignored = launchJoin()) {
            onView(withId(R.id.nearby_join_code_input)).check(matches(withText("")));
            onView(withId(R.id.nearby_join_submit)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_join_submit)).check(matches(not(isEnabled())));
        }
    }

    @Test public void incompleteCodeNeverEnablesTheRequest() {
        try (ActivityScenario<NearbyPairingActivity> ignored = launchJoin()) {
            onView(withId(R.id.nearby_join_code_input)).perform(typeText("12345"));
            onView(withId(R.id.nearby_join_submit)).check(matches(not(isEnabled())));
        }
    }

    @Test public void sixDigitsWithLeadingZeroEnableTheRequest() {
        try (ActivityScenario<NearbyPairingActivity> ignored = launchJoin()) {
            onView(withId(R.id.nearby_join_code_input)).perform(typeText("012345"));
            // Leading zero survives the input (C05).
            onView(withId(R.id.nearby_join_code_input)).check(matches(withText("012345")));
            onView(withId(R.id.nearby_join_submit)).check(matches(isEnabled()));
        }
    }

    @Test public void sevenDigitsArePreservedAndRejectedWithoutSilentTruncation() {
        try (ActivityScenario<NearbyPairingActivity> ignored = launchJoin()) {
            onView(withId(R.id.nearby_join_code_input))
                    .perform(replaceText("0123456"));
            onView(withId(R.id.nearby_join_code_input)).check(matches(withText("0123456")));
            onView(withId(R.id.nearby_join_submit)).check(matches(not(isEnabled())));
        }
    }

    @Test public void pasteSevenDigits1234567DoesNotTruncateOrSend() {
        try (ActivityScenario<NearbyPairingActivity> scenario = launchJoin()) {
            long before = consumeJoinAttemptFence();
            long attemptBefore = snapshotJoinAttemptId();
            onView(withId(R.id.nearby_join_code_input))
                    .perform(replaceText("1234567"),
                            androidx.test.espresso.action.ViewActions.closeSoftKeyboard());
            onView(withId(R.id.nearby_join_code_input)).check(matches(withText("1234567")));
            onView(withId(R.id.nearby_join_submit)).check(matches(not(isEnabled())));
            performSubmitClicks(scenario, 1);
            onView(withId(R.id.nearby_join_code_error)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_join_code_error))
                    .check(matches(withText(R.string.nearby_reason_code_invalidFormat)));
            org.junit.Assert.assertEquals("7-digit paste must not start a join attempt",
                    0L, joinSendsBetween(before, consumeJoinAttemptFence()));
            org.junit.Assert.assertEquals(attemptBefore, snapshotJoinAttemptId());
        }
    }

    @Test public void incompleteEmptyAndLettersNeverSend() {
        try (ActivityScenario<NearbyPairingActivity> scenario = launchJoin()) {
            assertInvalidNeverSends(scenario, "12345");
            assertInvalidNeverSends(scenario, "");
            assertInvalidNeverSends(scenario, "12A456");
        }
    }

    @Test public void sixDigitsSubmitTwiceIsOneAttempt() {
        try (ActivityScenario<NearbyPairingActivity> scenario = launchJoin()) {
            long before = consumeJoinAttemptFence();
            onView(withId(R.id.nearby_join_code_input)).perform(replaceText("123456"),
                    androidx.test.espresso.action.ViewActions.closeSoftKeyboard());
            onView(withId(R.id.nearby_join_code_input)).check(matches(withText("123456")));
            onView(withId(R.id.nearby_join_submit)).check(matches(isEnabled()));
            performSubmitClicks(scenario, 2);
            onView(withId(R.id.nearby_join_code_error)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_join_code_error))
                    .check(matches(withText(R.string.nearby_stage_discovery_reason)));
            org.junit.Assert.assertEquals("valid 123456 must send once even if submit is clicked twice",
                    1L, joinSendsBetween(before, consumeJoinAttemptFence()));
        }
    }

    @Test public void submitFailureAllowsModifyRetryAndCancelOnSamePage() {
        try (ActivityScenario<NearbyPairingActivity> scenario = launchJoin()) {
            long before = consumeJoinAttemptFence();
            onView(withId(R.id.nearby_join_code_input)).perform(replaceText("123456"),
                    androidx.test.espresso.action.ViewActions.closeSoftKeyboard());
            onView(withId(R.id.nearby_join_submit)).perform(click());
            onView(withId(R.id.nearby_join_code_error))
                    .check(matches(withText(R.string.nearby_stage_discovery_reason)));
            onView(withId(R.id.nearby_join_submit)).check(matches(isEnabled()));
            onView(withId(R.id.nearby_join_cancel)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_join_code_input)).perform(replaceText("654321"),
                    androidx.test.espresso.action.ViewActions.closeSoftKeyboard());
            onView(withId(R.id.nearby_join_submit)).perform(click());
            org.junit.Assert.assertEquals("failed attempt then edit must send a second lookup",
                    2L, joinSendsBetween(before, consumeJoinAttemptFence()));
            onView(withId(R.id.nearby_join_cancel)).perform(click());
            assertFinished(scenario);
        }
    }

    private static void assertInvalidNeverSends(
            ActivityScenario<NearbyPairingActivity> scenario, String raw) {
        long before = consumeJoinAttemptFence();
        long attemptBefore = snapshotJoinAttemptId();
        onView(withId(R.id.nearby_join_code_input)).perform(replaceText(raw),
                androidx.test.espresso.action.ViewActions.closeSoftKeyboard());
        onView(withId(R.id.nearby_join_code_input)).check(matches(withText(raw)));
        onView(withId(R.id.nearby_join_submit)).check(matches(not(isEnabled())));
        performSubmitClicks(scenario, 1);
        onView(withId(R.id.nearby_join_code_error)).check(matches(isDisplayed()));
        onView(withId(R.id.nearby_join_code_error))
                .check(matches(withText(R.string.nearby_reason_code_invalidFormat)));
        org.junit.Assert.assertEquals("invalid input must never call submitCode: " + raw,
                0L, joinSendsBetween(before, consumeJoinAttemptFence()));
        org.junit.Assert.assertEquals(attemptBefore, snapshotJoinAttemptId());
    }

    /** finish() is async across the EmptyActivity trampoline; wait past PAUSED. */
    private static void assertFinished(ActivityScenario<NearbyPairingActivity> scenario) {
        long deadline = android.os.SystemClock.elapsedRealtime() + 3000L;
        androidx.lifecycle.Lifecycle.State state = scenario.getState();
        while (state != androidx.lifecycle.Lifecycle.State.DESTROYED
                && android.os.SystemClock.elapsedRealtime() < deadline) {
            androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
                    .waitForIdleSync();
            android.os.SystemClock.sleep(50L);
            state = scenario.getState();
        }
        org.junit.Assert.assertEquals(androidx.lifecycle.Lifecycle.State.DESTROYED, state);
    }

    /** performClick fires the listener even when submit is disabled (MotionEvent does not). */
    private static void performSubmitClicks(
            ActivityScenario<NearbyPairingActivity> scenario, int times) {
        scenario.onActivity(activity -> {
            android.view.View submit = activity.findViewById(R.id.nearby_join_submit);
            for (int i = 0; i < times; i++) {
                submit.performClick();
            }
        });
    }

    @Test public void submitWithoutBearerShowsTheDiscoveryReasonNeverSuccess() {
        try (ActivityScenario<NearbyPairingActivity> ignored = launchJoin()) {
            onView(withId(R.id.nearby_join_code_input)).perform(typeText("012345"),
                    androidx.test.espresso.action.ViewActions.closeSoftKeyboard());
            onView(withId(R.id.nearby_join_submit)).perform(click());
            // No discovery bearer exists in this build: the honest outcome is the
            // blocking-stage reason, and no pairing state may appear.
            onView(withId(R.id.nearby_join_code_error)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_join_code_error))
                    .check(matches(withText(com.flynes.emu.R.string.nearby_stage_discovery_reason)));
        }
    }

    @Test public void createModeShowsLanQrLifecycleThatRegeneratesAndCancels() {
        Intent intent = new Intent(
                androidx.test.core.app.ApplicationProvider.getApplicationContext(),
                NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_CREATE);
        try (ActivityScenario<NearbyPairingActivity> scenario = ActivityScenario.launch(intent)) {
            onView(withId(R.id.nearby_create_block)).check(matches(isDisplayed()));
            Object firstQr = waitForQrChild(scenario);
            onView(withId(R.id.nearby_invite_regenerate)).perform(click());
            Object secondQr = waitForDifferentQrChild(scenario, firstQr);
            org.junit.Assert.assertNotSame("Regeneration must replace the rendered invitation",
                    firstQr, secondQr);

            onView(withId(R.id.nearby_invite_cancel)).perform(click());
            // Cancel finishes this page so the killed generation cannot stay on screen.
            assertFinished(scenario);
        }
    }

    private static Object waitForQrChild(ActivityScenario<NearbyPairingActivity> scenario) {
        final Object[] child = new Object[1];
        long deadline = android.os.SystemClock.elapsedRealtime() + 8000L;
        while (child[0] == null && android.os.SystemClock.elapsedRealtime() < deadline) {
            scenario.onActivity(activity -> {
                android.widget.FrameLayout frame = activity.findViewById(R.id.nearby_invite_qr);
                if (frame.getChildCount() == 1) child[0] = frame.getChildAt(0);
            });
            if (child[0] == null) android.os.SystemClock.sleep(50L);
        }
        org.junit.Assert.assertNotNull("Host did not publish a QR", child[0]);
        return child[0];
    }

    private static Object waitForDifferentQrChild(
            ActivityScenario<NearbyPairingActivity> scenario, Object previous) {
        final Object[] child = new Object[1];
        long deadline = android.os.SystemClock.elapsedRealtime() + 8000L;
        while ((child[0] == null || child[0] == previous)
                && android.os.SystemClock.elapsedRealtime() < deadline) {
            scenario.onActivity(activity -> {
                android.widget.FrameLayout frame = activity.findViewById(R.id.nearby_invite_qr);
                child[0] = frame.getChildCount() == 1 ? frame.getChildAt(0) : null;
            });
            if (child[0] == null || child[0] == previous) android.os.SystemClock.sleep(50L);
        }
        org.junit.Assert.assertNotNull("Replacement QR was not published", child[0]);
        return child[0];
    }
}
