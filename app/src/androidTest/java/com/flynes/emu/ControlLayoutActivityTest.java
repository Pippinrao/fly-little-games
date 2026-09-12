package com.flynes.emu;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertNotEquals;

import android.app.Activity;
import android.app.Instrumentation;
import android.content.Context;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import com.flynes.emu.input.ControlLayoutV2;
import com.flynes.emu.input.DirectionControlMode;
import com.flynes.emu.input.GamepadHitMap;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.ControlLayoutRepository;
import com.flynes.emu.settings.SettingsRepository;
import com.flynes.emu.settings.SettingsAccess;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class ControlLayoutActivityTest {
    @Test public void settingsIsTheOnlyEntryAndOpensEditor() {
        Instrumentation instrumentation = InstrumentationRegistry.getInstrumentation();
        Instrumentation.ActivityMonitor monitor = instrumentation.addMonitor(
                ControlLayoutActivity.class.getName(), null, false);
        Activity editor = null;
        try {
            try(ActivityScenario<SettingsActivity> scenario=
                        ActivityScenario.launch(SettingsActivity.class)) {
                scenario.onActivity(activity -> {
                    activity.findViewById(R.id.settings_controls_master).performClick();
                    activity.getSupportFragmentManager().executePendingTransactions();
                    com.flynes.emu.settings.SettingsFragment fragment =
                            (com.flynes.emu.settings.SettingsFragment) activity
                                    .getSupportFragmentManager()
                                    .findFragmentById(R.id.settings_content);
                    org.junit.Assert.assertNotNull(fragment);
                    androidx.preference.Preference preference =
                            fragment.findPreference("controls.layout_editor");
                    org.junit.Assert.assertNotNull(preference);
                    preference.performClick();
                });
                editor = instrumentation.waitForMonitorWithTimeout(monitor, 30_000L);
                org.junit.Assert.assertTrue(editor instanceof ControlLayoutActivity);
                Activity launchedEditor = editor;
                instrumentation.runOnMainSync(() -> {
                    org.junit.Assert.assertTrue(launchedEditor
                            .findViewById(R.id.control_layout_preview).isShown());
                    org.junit.Assert.assertTrue(launchedEditor
                            .findViewById(R.id.control_layout_save).isShown());
                });
                instrumentation.runOnMainSync(editor::finish);
                instrumentation.waitForIdleSync();
            }
        } finally {
            instrumentation.removeMonitor(monitor);
            if (editor != null && !editor.isFinishing()) {
                instrumentation.runOnMainSync(editor::finish);
            }
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
            scenario.onActivity(activity -> {
                View save = activity.findViewById(R.id.control_layout_save);
                org.junit.Assert.assertTrue(save.isShown());
                org.junit.Assert.assertTrue(save.performClick());
            });
        }
        ControlLayoutV2 saved=new ControlLayoutRepository((android.content.Context)
                androidx.test.core.app.ApplicationProvider.getApplicationContext()).load();
        assertNotEquals(before,saved);
        try(ActivityScenario<ControlLayoutActivity> restored=
                    ActivityScenario.launch(ControlLayoutActivity.class)) {
            restored.onActivity(activity -> org.junit.Assert.assertTrue(
                    activity.findViewById(R.id.control_layout_preview).isShown()));
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

    @Test public void editorHandlesAllDirectionModesNeutralCentersAndButtonPriority() {
        Context context = ApplicationProvider.getApplicationContext();

        ControlLayoutEditorView fixed = editor(context, DirectionControlMode.FIXED_JOYSTICK);
        GamepadHitMap fixedMap = editorMap(fixed, DirectionControlMode.FIXED_JOYSTICK);
        assertTrue(fixed.drawsJoystickForTest());
        assertEquals(context.getString(R.string.control_joystick),
                fixed.virtualControlNameForTest(0));
        assertEquals(ControlLayoutV2.Element.D_PAD, fixed.elementAtForTest(
                fixedMap.dpadBounds().centerX(), fixedMap.dpadBounds().centerY()));

        ControlLayoutV2 overlap = ControlLayoutV2.recommended().move(
                ControlLayoutV2.Element.A, .10f, .76f);
        fixed.setLayout(overlap);
        GamepadHitMap overlappingMap = editorMap(fixed,
                DirectionControlMode.FIXED_JOYSTICK);
        GamepadHitMap.Target overlappingA = overlappingMap.target(GamepadHitMap.Control.A);
        assertEquals(ControlLayoutV2.Element.A, fixed.elementAtForTest(
                overlappingA.centerX(), overlappingA.centerY()));

        ControlLayoutEditorView follow = editor(context, DirectionControlMode.JOYSTICK);
        assertTrue(follow.drawsJoystickForTest());
        assertEquals(context.getString(R.string.control_joystick),
                follow.virtualControlNameForTest(0));
        assertNull(follow.elementAtForTest(follow.getWidth() * .40f,
                follow.getHeight() * .50f));

        ControlLayoutEditorView dpad = editor(context, DirectionControlMode.DPAD);
        GamepadHitMap dpadMap = editorMap(dpad, DirectionControlMode.DPAD);
        assertFalse(dpad.drawsJoystickForTest());
        assertEquals(context.getString(R.string.control_dpad),
                dpad.virtualControlNameForTest(0));
        assertEquals(ControlLayoutV2.Element.D_PAD, dpad.elementAtForTest(
                dpadMap.dpadBounds().centerX(), dpadMap.dpadBounds().centerY()));
    }

    private static ControlLayoutEditorView editor(Context context,
                                                   DirectionControlMode mode) {
        context.getSharedPreferences(SettingsRepository.PREFERENCES_NAME, 0)
                .edit().clear().commit();
        SettingsAccess.repository(context).save(
                AppSettings.defaults().toBuilder().directionControlMode(mode).build());
        ControlLayoutEditorView view = new ControlLayoutEditorView(context);
        view.setLayout(ControlLayoutV2.recommended());
        view.measure(View.MeasureSpec.makeMeasureSpec(2340, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(1080, View.MeasureSpec.EXACTLY));
        view.layout(0, 0, 2340, 1080);
        return view;
    }

    private static GamepadHitMap editorMap(ControlLayoutEditorView view,
                                            DirectionControlMode mode) {
        return GamepadHitMap.fromLayout(view.getWidth(), view.getHeight(),
                view.getResources().getDisplayMetrics().density, 0, 0, 0, 0,
                view.layout(), mode, .18f);
    }
}
