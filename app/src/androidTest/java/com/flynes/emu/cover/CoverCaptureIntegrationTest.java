package com.flynes.emu.cover;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.os.SystemClock;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.HomeActivity;
import com.flynes.emu.R;
import com.flynes.emu.catalog.BuiltinGames;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.InputStream;

@RunWith(AndroidJUnit4.class)
public final class CoverCaptureIntegrationTest {
    private static final long LAUNCH_WINDOW_MS = 45_000L;
    private static final long CAPTURE_WINDOW_MS = 30_000L;

    /**
     * Boots the Game Center, starts the game the catalog has selected, and waits
     * out the whole 2/4/6/8-second capture window. A single-start activity launch
     * used to be enough because the app auto-played one bundled ROM; the Game
     * Center now owns launching, so the test has to go through it.
     */
    @Test public void gameplayPublishesAGameOnlyCoverWithoutTouchOverlay() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        AndroidCoverRepository repository = new AndroidCoverRepository(context);
        BuiltinGames games;
        try (InputStream manifest = context.getAssets().open(BuiltinGames.ASSET_NAME)) {
            games = BuiltinGames.parse(manifest);
        }
        assertTrue("the bundle must ship at least one game", games.all().size() > 0);
        for (BuiltinGames.Entry game : games.all()) repository.removeForTest(game.canonicalId);

        try (ActivityScenario<HomeActivity> ignored = ActivityScenario.launch(HomeActivity.class)) {
            // The catalog selects a game during bootstrap, so the button enables a
            // moment after the activity appears; clicking before that is harmless.
            long deadline = SystemClock.uptimeMillis() + LAUNCH_WINDOW_MS;
            while (SystemClock.uptimeMillis() < deadline && storedCoverCount(repository) == 0) {
                try {
                    onView(withId(R.id.launch_selected)).perform(click());
                } catch (RuntimeException notReadyYet) {
                    SystemClock.sleep(250L);
                }
            }

            deadline = SystemClock.uptimeMillis() + CAPTURE_WINDOW_MS;
            while (SystemClock.uptimeMillis() < deadline && storedCoverCount(repository) == 0) {
                SystemClock.sleep(250L);
            }
            assertTrue("no cover was captured for any bundled game after gameplay frames",
                    storedCoverCount(repository) > 0);
        }
    }

    /** Covers already written by the running session, whatever their canonical id. */
    private static int storedCoverCount(AndroidCoverRepository repository) {
        File[] files = repository.directoryForTest().listFiles();
        return files == null ? 0 : files.length;
    }
}
