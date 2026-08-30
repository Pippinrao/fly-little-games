package com.flynes.emu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import androidx.core.view.ViewCompat;
import androidx.core.view.accessibility.AccessibilityNodeInfoCompat;
import androidx.core.view.accessibility.AccessibilityNodeProviderCompat;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class GamepadAccessibilityTest {
    @Test public void gamepadExposesEightControlsAndVirtualAProducesInput() {
        try(ActivityScenario<ControlLayoutActivity> scenario=ActivityScenario.launch(ControlLayoutActivity.class)) {
            scenario.onActivity(activity->{
                GamepadView view=new GamepadView(activity);
                activity.addContentView(view,new android.view.ViewGroup.LayoutParams(1000,600));
                view.measure(android.view.View.MeasureSpec.makeMeasureSpec(1000,android.view.View.MeasureSpec.EXACTLY),
                        android.view.View.MeasureSpec.makeMeasureSpec(600,android.view.View.MeasureSpec.EXACTLY));
                view.layout(0,0,1000,600);
                AccessibilityNodeProviderCompat provider=ViewCompat.getAccessibilityNodeProvider(view);
                assertNotNull(provider);
                assertEquals(8,view.createAccessibilityNodeInfo().getChildCount());
                assertEquals(8,view.virtualControlCountForTest());
                assertEquals(activity.getString(R.string.control_a),view.virtualControlNameForTest(4));
                assertTrue(view.performVirtualControlClickForTest(4));
                assertTrue((view.buttons()&GamepadView.A)!=0);
            });
        }
    }
}
