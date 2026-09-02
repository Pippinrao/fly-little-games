package com.flynes.emu;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class JoystickReturnAnimationTest {
    @Test public void samplesStartMidpointAndCenterAtEightyMilliseconds() {
        JoystickReturnAnimation animation = new JoystickReturnAnimation();

        assertTrue(animation.start(10f, 20f, 50f, 100f, 1_000L));
        assertPosition(animation.sample(1_000L), 50f, 100f);
        assertPosition(animation.sample(1_040L), 30f, 60f);
        assertPosition(animation.sample(1_080L), 10f, 20f);
        assertFalse(animation.active());
    }

    @Test public void centeredKnobDoesNotStart() {
        JoystickReturnAnimation animation = new JoystickReturnAnimation();

        assertFalse(animation.start(10f, 20f, 10f, 20f, 1_000L));
        assertFalse(animation.active());
    }

    @Test public void cancellationClearsAnimationImmediately() {
        JoystickReturnAnimation animation = new JoystickReturnAnimation();
        animation.start(10f, 20f, 50f, 100f, 1_000L);

        animation.cancel();

        assertFalse(animation.active());
        assertPosition(animation.sample(1_020L), 10f, 20f);
    }

    @Test public void aNewStartInterruptsThePreviousReturn() {
        JoystickReturnAnimation animation = new JoystickReturnAnimation();
        animation.start(10f, 20f, 50f, 100f, 1_000L);

        assertTrue(animation.start(4f, 8f, 20f, 24f, 1_020L));

        assertPosition(animation.sample(1_060L), 12f, 16f);
        assertPosition(animation.sample(1_100L), 4f, 8f);
        assertFalse(animation.active());
    }

    private static void assertPosition(JoystickReturnAnimation.Sample actual,
                                       float expectedX, float expectedY) {
        assertEquals(expectedX, actual.x(), .001f);
        assertEquals(expectedY, actual.y(), .001f);
    }
}
