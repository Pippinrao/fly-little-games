package com.flynes.emu.ui;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.content.Intent;
import android.content.pm.ResolveInfo;
import android.view.View;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.HomeActivity;
import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class FirstRunNavigationTest {
    @Test
    public void firstRunShowsBuiltinLibraryAndSettingsActions() {
        try (ActivityScenario<HomeActivity> ignored = ActivityScenario.launch(HomeActivity.class)) {
            onView(withId(R.id.builtin_game)).check(matches(isDisplayed()));
            onView(withId(R.id.add_source)).check(matches(isDisplayed()));
            onView(withId(R.id.open_library)).check(matches(isDisplayed()));
            onView(withId(R.id.open_settings)).check(matches(isDisplayed()));
        }
    }

    @Test
    public void primaryActionsMeetAndroidTouchTargetMinimum() {
        try (ActivityScenario<HomeActivity> scenario = ActivityScenario.launch(HomeActivity.class)) {
            scenario.onActivity(activity -> {
                assertTouchTarget(activity.findViewById(R.id.builtin_game));
                assertTouchTarget(activity.findViewById(R.id.add_source));
                assertTouchTarget(activity.findViewById(R.id.open_library));
                assertTouchTarget(activity.findViewById(R.id.open_settings));
            });
        }
    }

    @Test
    public void homeActivityIsTheLauncher() {
        Intent intent = new Intent(Intent.ACTION_MAIN)
                .addCategory(Intent.CATEGORY_LAUNCHER)
                .setPackage(ApplicationProvider.getApplicationContext().getPackageName());
        ResolveInfo resolved = ApplicationProvider.getApplicationContext()
                .getPackageManager().resolveActivity(intent, 0);

        assertTrue(resolved != null);
        assertEquals(HomeActivity.class.getName(), resolved.activityInfo.name);
    }

    private static void assertTouchTarget(View view) {
        float density = view.getResources().getDisplayMetrics().density;
        assertTrue(view.getWidth() >= 48f * density);
        assertTrue(view.getHeight() >= 48f * density);
    }
}
