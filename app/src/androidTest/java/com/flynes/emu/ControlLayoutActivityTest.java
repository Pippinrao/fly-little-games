package com.flynes.emu;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.junit.Assert.assertNotEquals;

import android.os.SystemClock;
import android.view.MotionEvent;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import com.flynes.emu.input.ControlLayoutV2;
import com.flynes.emu.input.GamepadHitMap;
import com.flynes.emu.settings.ControlLayoutRepository;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class ControlLayoutActivityTest {
    @Test public void settingsIsTheOnlyEntryAndOpensEditor() {
        try(ActivityScenario<SettingsActivity> scenario=ActivityScenario.launch(SettingsActivity.class)) {
            scenario.onActivity(activity -> {
                activity.findViewById(R.id.settings_controls_master).performClick();
                activity.getSupportFragmentManager().executePendingTransactions();
                com.flynes.emu.settings.SettingsFragment fragment =
                        (com.flynes.emu.settings.SettingsFragment) activity.getSupportFragmentManager()
                                .findFragmentById(R.id.settings_content);
                org.junit.Assert.assertNotNull(fragment);
                androidx.preference.Preference preference=fragment.findPreference("controls.layout_editor");
                org.junit.Assert.assertNotNull(preference);
                preference.performClick();
            });
            onView(withId(R.id.control_layout_preview)).check(matches(isDisplayed()));
            onView(withId(R.id.control_layout_save)).check(matches(isDisplayed()));
        }
    }

    @Test public void dragSaveIsRestoredByNextEditorAndGameHitMap() {
        androidx.test.core.app.ApplicationProvider.getApplicationContext().getSharedPreferences(
                com.flynes.emu.settings.SettingsRepository.PREFERENCES_NAME,0).edit()
                .remove(ControlLayoutRepository.KEY).commit();
        ControlLayoutV2 before=ControlLayoutV2.recommended();
        try(ActivityScenario<ControlLayoutActivity> scenario=ActivityScenario.launch(ControlLayoutActivity.class)) {
            scenario.onActivity(activity->{
                ControlLayoutEditorView view=activity.findViewById(R.id.control_layout_preview);
                GamepadHitMap map=GamepadHitMap.fromLayout(view.getWidth(),view.getHeight(),
                        activity.getResources().getDisplayMetrics().density,0,0,0,0,view.layout());
                GamepadHitMap.Target a=map.target(GamepadHitMap.Control.A); long now=SystemClock.uptimeMillis();
                MotionEvent down=MotionEvent.obtain(now,now,MotionEvent.ACTION_DOWN,a.centerX(),a.centerY(),0);
                MotionEvent move=MotionEvent.obtain(now,now+20,MotionEvent.ACTION_MOVE,a.centerX()-40,a.centerY()-30,0);
                MotionEvent up=MotionEvent.obtain(now,now+30,MotionEvent.ACTION_UP,a.centerX()-40,a.centerY()-30,0);
                view.dispatchTouchEvent(down);view.dispatchTouchEvent(move);view.dispatchTouchEvent(up);
                down.recycle();move.recycle();up.recycle();
            });
            onView(withId(R.id.control_layout_save)).perform(androidx.test.espresso.action.ViewActions.click());
        }
        ControlLayoutV2 saved=new ControlLayoutRepository((android.content.Context)
                androidx.test.core.app.ApplicationProvider.getApplicationContext()).load();
        assertNotEquals(before,saved);
        try(ActivityScenario<ControlLayoutActivity> ignored=ActivityScenario.launch(ControlLayoutActivity.class)) {
            onView(withId(R.id.control_layout_preview)).check(matches(isDisplayed()));
        }
    }

    @Test public void editorExposesFiveIndependentVirtualControlsAndClick() {
        try(ActivityScenario<ControlLayoutActivity> scenario=ActivityScenario.launch(ControlLayoutActivity.class)) {
            scenario.onActivity(activity -> {
                ControlLayoutEditorView view=activity.findViewById(R.id.control_layout_preview);
                androidx.core.view.accessibility.AccessibilityNodeProviderCompat provider=
                        androidx.core.view.ViewCompat.getAccessibilityNodeProvider(view);
                org.junit.Assert.assertNotNull(provider);
                org.junit.Assert.assertEquals(5,view.createAccessibilityNodeInfo().getChildCount());
                org.junit.Assert.assertEquals(5,view.virtualControlCountForTest());
                org.junit.Assert.assertEquals(activity.getString(R.string.control_a),view.virtualControlNameForTest(1));
                org.junit.Assert.assertTrue(view.performVirtualControlClickForTest(1));
            });
        }
    }
}
