package com.flynes.emu;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.RootMatchers.isDialog;
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
                float density = activity.getResources().getDisplayMetrics().density;
                long now = SystemClock.uptimeMillis();
                float startX = gamepad.getWidth() / 2f + 28f * density;
                float startY = gamepad.getHeight() - 48f * density;
                MotionEvent down = MotionEvent.obtain(
                        now, now, MotionEvent.ACTION_DOWN, startX, startY, 0);
                MotionEvent up = MotionEvent.obtain(
                        now, now + 20, MotionEvent.ACTION_UP, startX, startY, 0);
                gamepad.dispatchTouchEvent(down);
                gamepad.dispatchTouchEvent(up);
                down.recycle();
                up.recycle();
            });

            onView(withText(R.string.pause_title)).check(doesNotExist());
            onView(withId(R.id.pause_button)).perform(androidx.test.espresso.action.ViewActions.click());
            onView(withText(R.string.pause_title)).inRoot(isDialog())
                    .check(matches(isDisplayed()));
        }
    }
}
