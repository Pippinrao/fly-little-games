package com.flynes.emu.ui;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.replaceText;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.ResolveInfo;
import android.os.SystemClock;
import android.graphics.Rect;
import android.os.ParcelFileDescriptor;
import android.text.Layout;
import android.view.View;
import android.widget.TextView;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.recyclerview.widget.RecyclerView;

import com.flynes.emu.HomeActivity;
import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class FirstRunNavigationTest {
    @Test
    public void firstRunShowsBuiltinLibraryAndSettingsActions() {
        try (ActivityScenario<HomeActivity> ignored = ActivityScenario.launch(HomeActivity.class)) {
            onView(withId(R.id.game_center_heading)).check(matches(isDisplayed()));
            onView(withId(R.id.category_recent)).check(matches(isDisplayed()));
            onView(withId(R.id.category_favorites)).check(matches(isDisplayed()));
            onView(withId(R.id.category_all)).check(matches(isDisplayed()));
            onView(withId(R.id.category_builtin)).check(matches(isDisplayed()));
            onView(withId(R.id.open_sources)).check(matches(isDisplayed()));
            onView(withId(R.id.open_settings)).check(matches(isDisplayed()));
        }
    }

    @Test
    public void primaryActionsMeetAndroidTouchTargetMinimum() {
        try (ActivityScenario<HomeActivity> scenario = ActivityScenario.launch(HomeActivity.class)) {
            scenario.onActivity(activity -> {
                assertTouchTarget(activity.findViewById(R.id.category_recent));
                assertTouchTarget(activity.findViewById(R.id.category_favorites));
                assertTouchTarget(activity.findViewById(R.id.category_all));
                assertTouchTarget(activity.findViewById(R.id.category_builtin));
                assertTouchTarget(activity.findViewById(R.id.open_search));
                assertTouchTarget(activity.findViewById(R.id.open_sources));
                assertTouchTarget(activity.findViewById(R.id.open_settings));
            });
        }
    }

    @Test public void sourceManagerStaysInsideTheGameCenter() {
        try (ActivityScenario<HomeActivity> scenario = ActivityScenario.launch(HomeActivity.class)) {
            scenario.onActivity(activity -> activity.findViewById(R.id.open_sources).performClick());
            onView(withId(R.id.source_content)).check(matches(isDisplayed()));
            onView(withId(R.id.add_source)).check(matches(isDisplayed()));
            scenario.onActivity(activity -> activity.findViewById(R.id.close_sources).performClick());
            onView(withId(R.id.game_center_content)).check(matches(isDisplayed()));
        }
    }

    @Test public void searchAndCategorySurviveActivityRecreation() {
        try (ActivityScenario<HomeActivity> scenario = ActivityScenario.launch(HomeActivity.class)) {
            onView(withId(R.id.category_builtin)).perform(click());
            onView(withId(R.id.open_search)).perform(click());
            onView(withId(R.id.search_input)).perform(replaceText("From"));
            scenario.recreate();
            onView(withId(R.id.search_input)).check(matches(withText("From")));
            onView(withId(R.id.category_builtin)).check(matches(isDisplayed()));
        }
    }

    @Test public void builtinUsesTheUnifiedLaunchPath() {
        try (ActivityScenario<HomeActivity> scenario = ActivityScenario.launch(HomeActivity.class)) {
            final boolean[] ready = {false};
            for (int attempt = 0; attempt < 80 && !ready[0]; attempt++) {
                scenario.onActivity(activity -> ready[0] =
                        activity.findViewById(R.id.launch_selected).isEnabled());
                if (!ready[0]) SystemClock.sleep(100L);
            }
            assertTrue("built-in launch did not become ready", ready[0]);
            android.app.Instrumentation.ActivityMonitor monitor =
                    androidx.test.platform.app.InstrumentationRegistry.getInstrumentation()
                            .addMonitor(com.flynes.emu.MainActivity.class.getName(), null, false);
            onView(withId(R.id.launch_selected)).perform(click());
            android.app.Activity launched = androidx.test.platform.app.InstrumentationRegistry
                    .getInstrumentation().waitForMonitorWithTimeout(monitor, 10_000L);
            assertTrue("MainActivity was not launched", launched != null);
            if (launched != null) launched.finish();
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

    @Test public void largeFontKeepsStatusCardAndCtaFullyVisible() throws Exception {
        shell("settings put system font_scale 2.0");
        try (ActivityScenario<HomeActivity> scenario = ActivityScenario.launch(HomeActivity.class)) {
            final boolean[] ready = {false};
            for (int attempt = 0; attempt < 80 && !ready[0]; attempt++) {
                scenario.onActivity(activity -> ready[0] =
                        activity.findViewById(R.id.launch_selected).isEnabled());
                if (!ready[0]) SystemClock.sleep(100L);
            }
            scenario.onActivity(activity -> {
                assertFullyVisible(activity.findViewById(R.id.library_status));
                assertFullyVisible(activity.findViewById(R.id.launch_selected));
                RecyclerView grid = activity.findViewById(R.id.game_grid);
                RecyclerView.ViewHolder holder = grid.findViewHolderForAdapterPosition(0);
                assertTrue("first large-font card was not laid out", holder != null);
                assertFullyVisible(holder.itemView.findViewById(R.id.card_title));
                assertFullyVisible(holder.itemView.findViewById(R.id.card_meta));
            });
            scenario.onActivity(activity -> activity.findViewById(R.id.open_sources).performClick());
            scenario.onActivity(activity -> {
                View copy = activity.findViewById(R.id.source_copy_scroll);
                View cta = activity.findViewById(R.id.add_source);
                Rect copyBounds = new Rect();
                Rect ctaBounds = new Rect();
                assertTrue(copy.getGlobalVisibleRect(copyBounds));
                assertTrue(cta.getGlobalVisibleRect(ctaBounds));
                assertTrue("source copy overlaps fixed CTA", copyBounds.bottom <= ctaBounds.top);
                assertTouchTarget(cta);
            });
        } finally {
            shell("settings put system font_scale 1.0");
        }
    }

    @Test public void bothLandscapeSensorDirectionsAreAllowed() {
        try (ActivityScenario<HomeActivity> scenario = ActivityScenario.launch(HomeActivity.class)) {
            scenario.onActivity(activity -> {
                assertEquals(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE,
                        activity.getRequestedOrientation());
                assertEquals(android.content.res.Configuration.ORIENTATION_LANDSCAPE,
                        activity.getResources().getConfiguration().orientation);
            });
        }
    }

    private static void assertTouchTarget(View view) {
        float density = view.getResources().getDisplayMetrics().density;
        assertTrue(view.getWidth() >= 48f * density);
        assertTrue(view.getHeight() >= 48f * density);
    }

    private static void assertFullyVisible(TextView view) {
        Rect visible = new Rect();
        assertTrue("text has no visible bounds", view.getGlobalVisibleRect(visible));
        assertTrue("text is vertically clipped", visible.height() >= view.getHeight());
        assertTrue("text is horizontally clipped", visible.width() >= view.getWidth());
        Layout layout = view.getLayout();
        assertTrue("text layout is missing", layout != null && layout.getLineCount() > 0);
        assertEquals("text layout omitted characters", view.getText().length(),
                layout.getLineEnd(layout.getLineCount() - 1));
        assertTrue("text layout exceeds view height",
                layout.getHeight() + view.getCompoundPaddingTop()
                        + view.getCompoundPaddingBottom() <= view.getHeight());
        int available = view.getWidth() - view.getCompoundPaddingLeft()
                - view.getCompoundPaddingRight();
        for (int line = 0; line < layout.getLineCount(); line++) {
            assertTrue("text line exceeds view width", layout.getLineWidth(line) <= available + 1f);
        }
    }

    private static void shell(String command) throws Exception {
        try (ParcelFileDescriptor ignored = InstrumentationRegistry.getInstrumentation()
                .getUiAutomation().executeShellCommand(command)) {
            SystemClock.sleep(400L);
        }
    }
}
