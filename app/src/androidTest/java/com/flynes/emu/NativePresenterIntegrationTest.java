package com.flynes.emu;

import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertEquals;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.lifecycle.Lifecycle;
import android.os.Build;

import com.flynes.emu.settings.FilterMode;
import com.flynes.emu.video.GameSurfaceView;
import com.flynes.emu.video.NativePresenterStats;
import com.flynes.emu.video.quality.SpatialMode;
import com.flynes.emu.video.status.VideoRuntimeStatus;
import com.flynes.emu.video.status.VideoStatusRepository;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.concurrent.atomic.AtomicReference;

@RunWith(AndroidJUnit4.class)
public final class NativePresenterIntegrationTest {
    @Test public void resolvedBaselineConfigurationIsPublishedAtomically() throws Exception {
        VideoStatusRepository.process().clear();
        try (ActivityScenario<MainActivity> ignored =
                     ActivityScenario.launch(MainActivity.class)) {
            long deadline = System.currentTimeMillis() + 5_000L;
            VideoRuntimeStatus status = null;
            while (System.currentTimeMillis() < deadline) {
                status = VideoStatusRepository.process().current();
                if (status != null && status.activeConfigurationId() != null) break;
                Thread.sleep(50L);
            }
            assertTrue("resolver never published a stable runtime configuration",
                    status != null && status.activeConfigurationId() != null);
            assertTrue(status.activeConfigurationId().startsWith("builtin:"));
            assertTrue("configuration id/key split publication",
                    status.activeConfigurationKey() != null);
            assertEquals(SpatialMode.SHARP_BILINEAR,
                    status.activeConfigurationKey().spatialMode());
        }
    }

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
            assertEquals(NativePresenterStats.PACING_OWNER_NATIVE, stats.pacingOwner());
        }
    }

    @Test public void nativeCoordinatorOwnsSourceRateVoteAndPauseClearsIt() throws Exception {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            AtomicReference<GameSurfaceView> surface = new AtomicReference<>();
            scenario.onActivity(activity -> surface.set((GameSurfaceView)
                    activity.findViewById(R.id.game_surface)));
            boolean virtualDevice = Build.FINGERPRINT.contains("generic")
                    || Build.HARDWARE.contains("ranchu")
                    || Build.HARDWARE.contains("goldfish");
            long deadline = System.currentTimeMillis() + 5_000L;
            NativePresenterStats active = NativePresenterStats.EMPTY;
            while (System.currentTimeMillis() < deadline) {
                active = surface.get().presenterStats();
                if (virtualDevice
                        ? active.frameRateVoteStatus()
                        == NativePresenterStats.FRAME_RATE_VOTE_CLEARED
                        : active.frameRateVoteStatus()
                        == NativePresenterStats.FRAME_RATE_VOTE_APPLIED) break;
                Thread.sleep(50L);
            }
            assertEquals(virtualDevice ? 0 : 60_099, active.requestedFrameRateMilliHz());
            assertEquals("frame-rate capability result did not fail closed",
                    virtualDevice ? NativePresenterStats.FRAME_RATE_VOTE_CLEARED
                            : NativePresenterStats.FRAME_RATE_VOTE_APPLIED,
                    active.frameRateVoteStatus());

            scenario.moveToState(Lifecycle.State.CREATED);
            deadline = System.currentTimeMillis() + 3_000L;
            NativePresenterStats paused = active;
            while (System.currentTimeMillis() < deadline) {
                paused = surface.get().presenterStats();
                if (paused.requestedFrameRateMilliHz() == 0) break;
                Thread.sleep(50L);
            }
            assertEquals(0, paused.requestedFrameRateMilliHz());
            assertEquals(NativePresenterStats.FRAME_RATE_VOTE_CLEARED,
                    paused.frameRateVoteStatus());
            scenario.moveToState(Lifecycle.State.RESUMED);
        }
    }

    @Test public void everyBaselineFilterCompilesAndSubmitsOnDevice() throws Exception {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            NativePresenterStats prior = awaitFrames(scenario, 10_000L);
            for (FilterMode mode : FilterMode.values()) {
                scenario.onActivity(activity -> ((GameSurfaceView)
                        activity.findViewById(R.id.game_surface)).setFilterMode(mode));
                NativePresenterStats next = awaitSubmissionAfter(scenario,
                        prior.submittedFrames(), 10_000L);
                assertTrue(mode + " produced no new native swap",
                        next.submittedFrames() > prior.submittedFrames());
                assertEquals(mode + " triggered an unexpected runtime fallback", 0L,
                        next.runtimeFailureCount());
                if (next.gpuTimingStatus() == NativePresenterStats.GPU_TIMING_VALID) {
                    assertTrue("valid GPU query did not report positive GPU nanoseconds",
                            next.lastGpuDurationNs() > 0L);
                } else {
                    assertEquals("unavailable/pending GPU timing must not expose CPU time",
                            -1L, next.lastGpuDurationNs());
                }
                prior = next;
            }
        }
    }

    @Test public void pauseStopsCallbacksAndResumeRecreatesAWorkingPresentationPath()
            throws Exception {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            NativePresenterStats before = awaitFrames(scenario, 10_000L);
            AtomicReference<GameSurfaceView> surface = new AtomicReference<>();
            scenario.onActivity(activity -> surface.set((GameSurfaceView)
                    activity.findViewById(R.id.game_surface)));
            scenario.moveToState(Lifecycle.State.CREATED);
            Thread.sleep(400L);
            NativePresenterStats paused = surface.get().presenterStats();
            Thread.sleep(400L);
            NativePresenterStats stillPaused = surface.get().presenterStats();
            assertTrue("pause left presentation callbacks running",
                    stillPaused.submittedFrames() - paused.submittedFrames() <= 1L);

            scenario.moveToState(Lifecycle.State.RESUMED);
            NativePresenterStats resumed = awaitSubmissionAfter(scenario,
                    stillPaused.submittedFrames(), 10_000L);
            assertTrue("resume produced no swap; paused=" + stillPaused
                            + ", resumed=" + resumed,
                    resumed.submittedFrames() > stillPaused.submittedFrames());
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
