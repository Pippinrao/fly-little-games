package com.flynes.emu;

import static org.junit.Assert.assertEquals;

import android.content.res.Configuration;

import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class ProductOrientationTest {
    @Test public void libraryAndLicensesKeepTheLandscapeHandPosition() {
        try (ActivityScenario<GameLibraryActivity> library =
                     ActivityScenario.launch(GameLibraryActivity.class)) {
            library.onActivity(activity -> assertEquals(Configuration.ORIENTATION_LANDSCAPE,
                    activity.getResources().getConfiguration().orientation));
        }
        try (ActivityScenario<LicensesActivity> licenses =
                     ActivityScenario.launch(LicensesActivity.class)) {
            licenses.onActivity(activity -> assertEquals(Configuration.ORIENTATION_LANDSCAPE,
                    activity.getResources().getConfiguration().orientation));
        }
    }
}
