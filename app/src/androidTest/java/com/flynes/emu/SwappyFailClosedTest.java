package com.flynes.emu;

import static org.junit.Assert.assertFalse;

import android.os.SystemClock;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.lifecycle.Lifecycle;

import com.flynes.emu.video.GameSurfaceView;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class SwappyFailClosedTest {
    @Test public void incompatibleModeAndDisabledPacerCannotActivateMotion() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                GameSurfaceView surface = activity.findViewById(R.id.game_surface);
                surface.onPause();
                long deadline = SystemClock.elapsedRealtimeNanos() + 1_500_000_000L;
                assertFalse(surface.configureMotionForTesting(
                        1L, 60.0f, 60.0988f, deadline, false));
                assertFalse(surface.configureMotionForTesting(
                        2L, 120.0f, 60.0988f, deadline, true));
                surface.onResume();
            });
            // Complete MainActivity's bounded pause/surface transaction before
            // ActivityScenario closes the activity. Under a full-suite emulator
            // load, combining both transitions can exhaust ActivityScenario's
            // destroy wait even though the fail-closed assertions have passed.
            scenario.moveToState(Lifecycle.State.CREATED);
        }
    }
}
