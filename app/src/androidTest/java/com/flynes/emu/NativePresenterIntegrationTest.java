package com.flynes.emu;

import static org.junit.Assert.assertTrue;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.lifecycle.Lifecycle;

import com.flynes.emu.settings.FilterMode;
import com.flynes.emu.video.GameSurfaceView;
import com.flynes.emu.video.NativePresenterStats;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.concurrent.atomic.AtomicReference;

@RunWith(AndroidJUnit4.class)
public final class NativePresenterIntegrationTest {
    @Test public void gameplayUsesOneEventDrivenNativeSurfaceOwner() throws Exception {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> assertTrue(
                    activity.findViewById(R.id.game_surface) instanceof GameSurfaceView));
            NativePresenterStats stats = awaitFrames(scenario, 3_000L);
            assertTrue("native presenter did not upload a source frame", stats.uploadedFrames() > 0L);
            assertTrue("native presenter did not swap a source frame", stats.submittedFrames() > 0L);
            assertTrue("a swap cannot exist without its source upload",
                    stats.uploadedFrames() >= stats.submittedFrames());
            assertTrue("at most the currently rendering upload may be awaiting swap",
                    stats.uploadedFrames() - stats.submittedFrames() <= 1L);
            assertTrue(stats.lastSequence() >= 0L);
            assertTrue(stats.surfaceEpoch() > 0L);
        }
    }

    @Test public void everyBaselineFilterCompilesAndSubmitsOnDevice() throws Exception {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            NativePresenterStats prior = awaitFrames(scenario, 3_000L);
            for (FilterMode mode : FilterMode.values()) {
                scenario.onActivity(activity -> ((GameSurfaceView)
                        activity.findViewById(R.id.game_surface)).setFilterMode(mode));
                NativePresenterStats next = awaitSubmissionAfter(scenario,
                        prior.submittedFrames(), 3_000L);
                assertTrue(mode + " produced no new native swap",
                        next.submittedFrames() > prior.submittedFrames());
                prior = next;
            }
        }
    }

    @Test public void pauseStopsCallbacksAndResumeRecreatesAWorkingPresentationPath()
            throws Exception {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            NativePresenterStats before = awaitFrames(scenario, 3_000L);
            scenario.moveToState(Lifecycle.State.CREATED);
            Thread.sleep(400L);
            NativePresenterStats paused = observe(scenario);
            Thread.sleep(400L);
            NativePresenterStats stillPaused = observe(scenario);
            assertTrue("pause left presentation callbacks running",
                    stillPaused.submittedFrames() - paused.submittedFrames() <= 1L);

            scenario.moveToState(Lifecycle.State.RESUMED);
            NativePresenterStats resumed = awaitSubmissionAfter(scenario,
                    stillPaused.submittedFrames(), 3_000L);
            assertTrue(resumed.submittedFrames() > stillPaused.submittedFrames());
            assertTrue(resumed.surfaceEpoch() >= before.surfaceEpoch());
        }
    }

    private static NativePresenterStats awaitFrames(ActivityScenario<MainActivity> scenario,
                                                     long timeoutMs) throws Exception {
        return awaitSubmissionAfter(scenario, 0L, timeoutMs);
    }

    private static NativePresenterStats awaitSubmissionAfter(
            ActivityScenario<MainActivity> scenario, long previous, long timeoutMs) throws Exception {
        long deadline = System.currentTimeMillis() + timeoutMs;
        NativePresenterStats value = NativePresenterStats.EMPTY;
        while (System.currentTimeMillis() < deadline) {
            value = observe(scenario);
            if (value != null && value.submittedFrames() > previous) return value;
            Thread.sleep(50L);
        }
        return value;
    }

    private static NativePresenterStats observe(ActivityScenario<MainActivity> scenario) {
        AtomicReference<NativePresenterStats> observed = new AtomicReference<>();
        scenario.onActivity(activity -> observed.set(((GameSurfaceView)
                activity.findViewById(R.id.game_surface)).presenterStats()));
        return observed.get();
    }
}
