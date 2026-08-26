package com.flynes.emu;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class GamepadViewTest {

    @Test
    public void centerAfterFollow_movesOnlyTheDistancePastTheTravelLimit() {
        float[] center = GamepadView.centerAfterFollow(100f, 100f, 160f, 100f, 40f);

        assertEquals(120f, center[0], 0.001f);
        assertEquals(100f, center[1], 0.001f);
    }

    @Test
    public void centerAfterFollow_keepsCenterWhenFingerIsWithinTravelLimit() {
        float[] center = GamepadView.centerAfterFollow(100f, 100f, 130f, 100f, 40f);

        assertEquals(100f, center[0], 0.001f);
        assertEquals(100f, center[1], 0.001f);
    }

    @Test
    public void isJoystickStart_acceptsAnyTouchInTheLeftHalf() {
        assertEquals(true, GamepadView.isJoystickStart(20f, 400f));
        assertEquals(true, GamepadView.isJoystickStart(199f, 400f));
    }

    @Test
    public void isJoystickStart_rejectsTouchesInTheRightHalf() {
        assertEquals(false, GamepadView.isJoystickStart(200f, 400f));
    }
}
