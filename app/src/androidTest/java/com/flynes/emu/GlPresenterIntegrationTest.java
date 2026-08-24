package com.flynes.emu;

import static org.junit.Assert.assertTrue;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.settings.FilterMode;
import com.flynes.emu.video.GlFrameView;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class GlPresenterIntegrationTest {
    @Test
    public void gameplayUsesTheSequencedOpenGlSurface() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> assertTrue(
                    activity.findViewById(R.id.game_surface) instanceof GlFrameView));
        }
    }

    @Test
    public void everySelectableGpuFilterCompilesOnDevice() throws Exception {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            for (FilterMode mode : FilterMode.values()) {
                scenario.onActivity(activity -> ((GlFrameView)
                        activity.findViewById(R.id.game_surface)).setFilterMode(mode));
                Thread.sleep(250L);
                scenario.onActivity(activity -> assertTrue(!activity.isFinishing()));
            }
        }
    }
}
