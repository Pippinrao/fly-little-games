package com.flynes.emu.test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.Intent;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.MainActivity;
import com.flynes.emu.R;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.SettingsRepository;
import com.flynes.emu.settings.SharedPreferencesSettingsStore;
import com.flynes.emu.video.GameSurfaceView;
import com.flynes.emu.video.NativePresenterStats;
import com.flynes.emu.video.quality.CustomVideoSettings;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;
import com.flynes.emu.video.quality.PostEffect;
import com.flynes.emu.video.quality.SpatialMode;
import com.flynes.emu.video.quality.TemporalMode;
import com.flynes.emu.video.quality.VideoPreferences;
import com.flynes.emu.video.quality.VideoQualityPreset;
import com.flynes.emu.video.status.VideoRuntimeStatus;
import com.flynes.emu.video.status.VideoStatusRepository;

import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.concurrent.atomic.AtomicReference;

/** Physical proof that 120 Hz native mode stays source-driven instead of duplicating work. */
@DeviceCertification
@RunWith(AndroidJUnit4.class)
public final class NativeTime120CertificationTest {
    @BeforeClass public static void requireAuthorizedPhysicalDevice() {
        assertTrue("120 Hz certification requires an explicitly authorized physical device",
                SingleDeviceCertificationRunner.isAuthorized());
    }

    @Test public void nativeTimeAt120HzSubmitsOnlyUniqueSourceFrames() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        SettingsRepository repository = new SettingsRepository(
                new SharedPreferencesSettingsStore(context));
        AppSettings old = repository.load();
        VideoPreferences request = new VideoPreferences(VideoQualityPreset.CUSTOM,
                new CustomVideoSettings(PhysicalRefreshPolicy.HZ_120, TemporalMode.NATIVE,
                        SpatialMode.SHARP_BILINEAR, PostEffect.NONE), false);
        assertTrue(repository.save(old.toBuilder().videoPreferences(request).build()));
        VideoStatusRepository.process().clear();
        Intent certification = new Intent(context, MainActivity.class)
                .putExtra("com.flynes.emu.extra.CERTIFY_NATIVE_TIME_MODE", "120");
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(certification)) {
            NativePresenterStats voted = awaitAppliedVote(scenario, 10_000L);
            assertEquals(60_099, voted.requestedFrameRateMilliHz());
            assertEquals(NativePresenterStats.FRAME_RATE_VOTE_APPLIED,
                    voted.frameRateVoteStatus());

            VideoRuntimeStatus status = awaitActive120Hz(10_000L);
            assertNotNull("display status was never published", status);
            assertNotNull("system active mode was never observed",
                    status.systemReportedActiveMode());
            int actualMilliHz = status.systemReportedActiveMode().refreshMilliHz();
            assertTrue("device did not settle at 120 Hz: " + actualMilliHz,
                    Math.abs(actualMilliHz - 120_000) <= 1_001);
            assertEquals(0f, status.synthesizedSlotSubmitFps(), 0.01f);
            assertEquals(0f, status.motionWarpedSlotFps(), 0.01f);

            // Start the three-second count only after both independent conditions have
            // settled; otherwise display-observation latency contaminates the interval.
            NativePresenterStats start = observe(scenario);
            Thread.sleep(3_000L);
            NativePresenterStats end = observe(scenario);
            long submissions = end.submittedFrames() - start.submittedFrames();
            long uploads = end.uploadedFrames() - start.uploadedFrames();
            assertTrue("native-time path was not near 60 unique submits/s: " + submissions,
                    submissions >= 135L && submissions <= 210L);
            assertTrue("uploads and submits diverged: " + uploads + '/' + submissions,
                    uploads >= submissions && uploads - submissions <= 1L);
        } finally {
            repository.save(old);
        }
    }

    private static NativePresenterStats awaitAppliedVote(
            ActivityScenario<MainActivity> scenario, long timeoutMs) throws Exception {
        long deadline = System.currentTimeMillis() + timeoutMs;
        NativePresenterStats stats = NativePresenterStats.EMPTY;
        while (System.currentTimeMillis() < deadline) {
            stats = observe(scenario);
            if (stats.requestedFrameRateMilliHz() == 60_099
                    && stats.frameRateVoteStatus()
                    == NativePresenterStats.FRAME_RATE_VOTE_APPLIED) return stats;
            Thread.sleep(50L);
        }
        return stats;
    }

    private static VideoRuntimeStatus awaitActive120Hz(long timeoutMs) throws Exception {
        long deadline = System.currentTimeMillis() + timeoutMs;
        VideoRuntimeStatus status = null;
        while (System.currentTimeMillis() < deadline) {
            status = VideoStatusRepository.process().current();
            if (status != null && status.systemReportedActiveMode() != null
                    && Math.abs(status.systemReportedActiveMode().refreshMilliHz() - 120_000)
                    <= 1_001) return status;
            Thread.sleep(50L);
        }
        return status;
    }

    private static NativePresenterStats observe(ActivityScenario<MainActivity> scenario) {
        AtomicReference<NativePresenterStats> result = new AtomicReference<>(
                NativePresenterStats.EMPTY);
        scenario.onActivity(activity -> result.set(((GameSurfaceView)
                activity.findViewById(R.id.game_surface)).presenterStats()));
        return result.get();
    }
}
