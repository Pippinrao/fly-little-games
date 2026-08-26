package com.flynes.emu;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;

import android.os.SystemClock;
import android.view.MotionEvent;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class StartAndPauseSeparationTest {
    @Test
    public void nesStartDoesNotOpenPauseButPauseButtonDoes() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                GamepadView gamepad = activity.findViewById(R.id.gamepad);
                com.flynes.emu.input.GamepadHitMap.Target start = gamepad.hitMapForTest()
                        .target(com.flynes.emu.input.GamepadHitMap.Control.START);
                long now = SystemClock.uptimeMillis();
                MotionEvent down = MotionEvent.obtain(
                        now, now, MotionEvent.ACTION_DOWN, start.centerX(), start.centerY(), 0);
                MotionEvent up = MotionEvent.obtain(
                        now, now + 20, MotionEvent.ACTION_UP, start.centerX(), start.centerY(), 0);
                gamepad.dispatchTouchEvent(down);
                gamepad.dispatchTouchEvent(up);
                down.recycle();
                up.recycle();
            });

            onView(withText(R.string.pause_title)).check(doesNotExist());
            final long[] openMillis = {0L};
            scenario.onActivity(activity -> {
                long before = SystemClock.uptimeMillis();
                activity.findViewById(R.id.pause_button).performClick();
                openMillis[0] = SystemClock.uptimeMillis() - before;
            });
            org.junit.Assert.assertTrue("pause drawer blocked main thread for " + openMillis[0] + "ms",
                    openMillis[0] <= 150L);
            SystemClock.sleep(240L);
            onView(withId(R.id.pause_drawer)).check(matches(isDisplayed()));
            long clearDeadline = SystemClock.uptimeMillis() + 2_000L;
            com.flynes.emu.video.NativePresenterStats pauseStats =
                    com.flynes.emu.video.NativePresenterStats.EMPTY;
            while (SystemClock.uptimeMillis() < clearDeadline) {
                final java.util.concurrent.atomic.AtomicReference<
                        com.flynes.emu.video.NativePresenterStats> observed =
                        new java.util.concurrent.atomic.AtomicReference<>();
                scenario.onActivity(activity -> observed.set(((
                        com.flynes.emu.video.GameSurfaceView) activity.findViewById(
                        R.id.game_surface)).presenterStats()));
                pauseStats = observed.get();
                if (pauseStats.requestedFrameRateMilliHz() == 0) break;
                SystemClock.sleep(25L);
            }
            org.junit.Assert.assertEquals(0, pauseStats.requestedFrameRateMilliHz());
            org.junit.Assert.assertEquals(
                    com.flynes.emu.video.NativePresenterStats.FRAME_RATE_VOTE_CLEARED,
                    pauseStats.frameRateVoteStatus());
            onView(withText(R.string.control_layout_title)).check(doesNotExist());
            onView(withId(R.id.pause_game_title)).check(matches(withText(R.string.builtin_game_name)));
            scenario.onActivity(activity -> {
                android.graphics.Rect buttonBounds = new android.graphics.Rect();
                android.view.View continueButton = activity.findViewById(R.id.pause_continue);
                org.junit.Assert.assertTrue(continueButton.getGlobalVisibleRect(buttonBounds));
                android.view.WindowInsets insets = activity.findViewById(R.id.pause_layer)
                        .getRootWindowInsets();
                int navigationLeft = activity.findViewById(R.id.pause_layer).getWidth()
                        - (insets == null ? 0 : insets.getSystemWindowInsetRight());
                org.junit.Assert.assertTrue("continue enters system navigation bounds",
                        buttonBounds.right <= navigationLeft);
            });
            onView(withId(R.id.pause_scrim)).perform(androidx.test.espresso.action.ViewActions.click());
            onView(withId(R.id.pause_drawer)).check(doesNotExist());

            onView(withId(R.id.pause_button)).perform(androidx.test.espresso.action.ViewActions.click());
            SystemClock.sleep(240L);
            onView(withId(R.id.pause_continue)).perform(androidx.test.espresso.action.ViewActions.click());
            onView(withId(R.id.pause_drawer)).check(doesNotExist());

            onView(withId(R.id.pause_button)).perform(androidx.test.espresso.action.ViewActions.click());
            SystemClock.sleep(240L);
            androidx.test.espresso.Espresso.pressBack();
            onView(withId(R.id.pause_drawer)).check(doesNotExist());
        }
    }

    @Test public void controlsKeepErgonomicBoundsAndPauseIsAppOnly() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                GamepadView view = activity.findViewById(R.id.gamepad);
                com.flynes.emu.input.GamepadHitMap map = view.hitMapForTest();
                float density = activity.getResources().getDisplayMetrics().density;
                org.junit.Assert.assertTrue(map.distanceBetween(
                        map.target(com.flynes.emu.input.GamepadHitMap.Control.A),
                        map.target(com.flynes.emu.input.GamepadHitMap.Control.B)) >= 24f * density);
                // On compact/notched devices the safe insets consume part of the outer 15%;
                // the system pills must still remain in their respective edge quarters.
                org.junit.Assert.assertTrue(map.target(com.flynes.emu.input.GamepadHitMap.Control.SELECT).right()
                        <= view.getWidth() * .25f);
                org.junit.Assert.assertTrue(map.target(com.flynes.emu.input.GamepadHitMap.Control.START).left()
                        >= view.getWidth() * .75f);
                android.view.View pause = activity.findViewById(R.id.pause_button);
                org.junit.Assert.assertTrue(pause.getWidth() >= 48f * density);
                org.junit.Assert.assertTrue(pause.getHeight() >= 48f * density);
            });
        }
    }
}
