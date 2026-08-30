package com.flynes.emu.cover;

import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.os.SystemClock;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.MainActivity;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class CoverCaptureIntegrationTest {
    @Test public void gameplayPublishesAGameOnlyCoverWithoutTouchOverlay() {
        Context context = ApplicationProvider.getApplicationContext();
        AndroidCoverRepository repository = new AndroidCoverRepository(context);
        repository.removeForTest("builtin:from-below");

        try (ActivityScenario<MainActivity> ignored = ActivityScenario.launch(MainActivity.class)) {
            for (int attempt = 0; attempt < 80 && !repository.exists("builtin:from-below"); attempt++) {
                SystemClock.sleep(100L);
            }
            assertTrue("no cover was captured after gameplay frames",
                    repository.exists("builtin:from-below"));
        }
    }
}
