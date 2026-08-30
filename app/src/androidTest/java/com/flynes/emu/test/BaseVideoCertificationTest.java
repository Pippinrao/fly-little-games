package com.flynes.emu.test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.content.Context;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.MainActivity;
import com.flynes.emu.R;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.FilterMode;
import com.flynes.emu.settings.SettingsRepository;
import com.flynes.emu.settings.SharedPreferencesSettingsStore;
import com.flynes.emu.video.GameSurfaceView;
import com.flynes.emu.video.NativePresenterStats;
import com.flynes.emu.video.quality.LegacyVideoRuntimeAdapter;
import com.flynes.emu.video.quality.VideoPreferences;
import com.flynes.emu.video.quality.VideoQualityPreset;

import org.junit.BeforeClass;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.concurrent.atomic.AtomicReference;

@DeviceCertification
@RunWith(AndroidJUnit4.class)
public final class BaseVideoCertificationTest {
    @BeforeClass public static void requireAuthorizedPhysicalDevice() {
        assertTrue("Device certification requires the explicit single-device runner transaction",
                SingleDeviceCertificationRunner.isAuthorized());
    }

    @Test public void native60NearestBaseRow() throws Exception {
        certify(VideoQualityPreset.POWER_SAVER, FilterMode.NEAREST);
    }

    @Test public void balancedSharpBaseRow() throws Exception {
        certify(VideoQualityPreset.BALANCED, FilterMode.SHARP_BILINEAR);
    }

    private static void certify(VideoQualityPreset preset, FilterMode expectedFilter)
            throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        SettingsRepository repository = new SettingsRepository(
                new SharedPreferencesSettingsStore(context));
        AppSettings old = repository.load();
        VideoPreferences selected = new VideoPreferences(preset,
                old.videoPreferences().custom(), true);
        assertTrue(repository.save(old.toBuilder().videoPreferences(selected).build()));
        assertEquals(expectedFilter, LegacyVideoRuntimeAdapter.project(selected).rendererFilter());
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            NativePresenterStats stats = awaitFrames(scenario, 5_000L);
            assertTrue(stats.submittedFrames() >= 120L);
            assertTrue(stats.uploadedFrames() >= stats.submittedFrames());
            assertTrue(stats.uploadedFrames() - stats.submittedFrames() <= 1L);
            assertTrue(stats.surfaceEpoch() > 0L);
        } finally {
            repository.save(old);
        }
    }

    private static NativePresenterStats awaitFrames(ActivityScenario<MainActivity> scenario,
                                                     long timeoutMs) throws Exception {
        long deadline = System.currentTimeMillis() + timeoutMs;
        NativePresenterStats value = NativePresenterStats.EMPTY;
        while (System.currentTimeMillis() < deadline) {
            AtomicReference<NativePresenterStats> observed = new AtomicReference<>();
            scenario.onActivity(activity -> observed.set(((GameSurfaceView)
                    activity.findViewById(R.id.game_surface)).presenterStats()));
            value = observed.get();
            if (value != null && value.submittedFrames() >= 120L) return value;
            Thread.sleep(50L);
        }
        return value;
    }
}
