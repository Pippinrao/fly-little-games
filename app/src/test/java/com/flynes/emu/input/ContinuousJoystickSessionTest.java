package com.flynes.emu.input;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public final class ContinuousJoystickSessionTest {
    private final GamepadHitMap map = GamepadHitMap.fromLayout(
            2340, 1080, 2.75f, 0, 132, 0, 0,
            ControlLayoutV2.recommended(), DirectionControlMode.JOYSTICK, .22f);

    @Test public void centerDownIsCapturedBeforeDirectionBegins() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        assertTrue(state.down(1, base.centerX(), base.centerY(), 1L));
        assertEquals(0, state.mask());
        assertEquals(1, state.activePointerCount());

        state.move(1, base.centerX() + base.width(), base.centerY(), 2L);
        assertEquals(InputBits.RIGHT, state.mask());
    }

    @Test public void downOutsideIdleBaseIsCapturedNeutralThenMoves() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float radius = map.joystickRadius();
        float x = base.centerX() + radius * 2f;

        assertTrue(state.down(4, x, base.centerY(), 1L));
        assertEquals(0, state.mask());
        assertEquals(1, state.activePointerCount());

        state.move(4, x + radius, base.centerY(), 2L);
        assertEquals(InputBits.RIGHT, state.mask());
    }

    @Test public void edgeClampedDownStartsNeutralAndUsesPhysicalMovement() {
        GamepadInputState state = new GamepadInputState(map);
        float radius = map.joystickRadius();

        assertTrue(state.down(1, 1f, 1f, 1L));

        GamepadInputState.JoystickVisual down = state.joystickVisual();
        assertTrue(down.active());
        assertEquals(1, state.activePointerCount());
        assertEquals(0, state.mask());
        assertEquals(radius, down.centerX(), .01f);
        assertEquals(radius, down.centerY(), .01f);
        assertEquals(down.centerX(), down.knobX(), .01f);
        assertEquals(down.centerY(), down.knobY(), .01f);

        state.move(1, 1f + radius * .25f, 1f, 2L);

        assertEquals(InputBits.RIGHT, state.mask());
        assertTrue(state.hasConsistentOwnership());
    }

    @Test public void dragPastFiveRadiiStaysDirectionalAndReversesWithoutLift() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float radius = base.width() / 2f;
        state.down(7, base.centerX(), base.centerY(), 1L);

        state.move(7, base.centerX() + radius * 5f, base.centerY(), 2L);
        assertEquals(InputBits.RIGHT, state.mask());
        assertEquals(1, state.activePointerCount());

        state.move(7, base.centerX() - radius * 5f, base.centerY(), 3L);
        assertEquals(InputBits.LEFT, state.mask());
        assertEquals(1, state.activePointerCount());
    }

    @Test public void secondLeftPointerCannotStealWhileAStillCombines() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        GamepadHitMap.Target a = map.target(GamepadHitMap.Control.A);
        assertTrue(state.down(1, base.centerX(), base.centerY(), 1L));
        assertFalse(state.down(2, base.centerX() + 80f, base.centerY(), 2L));
        assertEquals(1, state.activePointerCount());

        assertTrue(state.down(3, a.centerX(), a.centerY(), 3L));
        state.move(1, base.centerX() + base.width(), base.centerY(), 4L);
        assertEquals(InputBits.RIGHT | InputBits.A, state.mask());
        assertEquals(2, state.activePointerCount());
        assertTrue(state.hasConsistentOwnership());
    }

    @Test public void leftSideSelectTargetWinsOverJoystickActivation() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Target select = map.target(GamepadHitMap.Control.SELECT);
        assertTrue(state.down(9, select.centerX(), select.centerY(), 1L));
        assertEquals(InputBits.SELECT, state.mask());
        assertFalse(state.joystickVisual().active());
    }

    @Test public void joystickOwnerKeepsRoleAcrossActionButtonCoordinates() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        GamepadHitMap.Target a = map.target(GamepadHitMap.Control.A);
        state.down(1, base.centerX(), base.centerY(), 1L);

        state.move(1, a.centerX(), a.centerY(), 2L);

        assertTrue(state.joystickVisual().active());
        assertEquals(1, state.activePointerCount());
        assertEquals(0, state.mask() & InputBits.A);
        assertTrue((state.mask() & InputBits.RIGHT) != 0);
    }

    @Test public void baseFollowsOnlyExcessAndKnobNeverExceedsTravel() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float travel = map.joystickTravelRadius();
        state.down(1, base.centerX(), base.centerY(), 1L);
        state.move(1, base.centerX() + travel * 2f, base.centerY(), 2L);

        GamepadInputState.JoystickVisual visual = state.joystickVisual();
        assertTrue(visual.active());
        assertEquals(travel, visual.centerX() - base.centerX(), .01f);
        assertEquals(travel, visual.knobX() - visual.centerX(), .01f);
        assertTrue(Math.hypot(visual.knobX() - visual.centerX(),
                visual.knobY() - visual.centerY()) <= travel + .01f);
    }

    @Test public void dynamicCenterIsClampedInsideSafeLeftHalf() {
        GamepadInputState state = new GamepadInputState(map);
        float radius = map.joystickRadius();

        assertTrue(state.down(1, 1f, 1f, 1L));
        GamepadInputState.JoystickVisual topLeft = state.joystickVisual();
        assertEquals(radius, topLeft.centerX(), .01f);
        assertEquals(radius, topLeft.centerY(), .01f);
        state.up(1, 2L);

        float safeHalfRight = (2340f - 132f) / 2f;
        assertTrue(state.down(2, safeHalfRight - 1f, 1079f, 3L));
        GamepadInputState.JoystickVisual bottomRight = state.joystickVisual();
        assertEquals(safeHalfRight - radius, bottomRight.centerX(), .01f);
        assertEquals(1080f - radius, bottomRight.centerY(), .01f);
    }

    @Test public void insetReconfigurationPreservesVectorAndFutureMotion() {
        GamepadInputState state = new GamepadInputState(map);
        float radius = map.joystickRadius();
        float x = radius;
        float y = 500f;
        assertTrue(state.down(1, x, y, 1L));
        state.move(1, x + radius * .25f, y, 2L);
        assertEquals(InputBits.RIGHT, state.mask());
        GamepadInputState.JoystickVisual before = state.joystickVisual();
        GamepadHitMap insetMap = GamepadHitMap.fromLayout(
                2340, 1080, 2.75f, 80, 132, 20, 30,
                ControlLayoutV2.recommended(), DirectionControlMode.JOYSTICK, .22f);

        state.reconfigure(insetMap);

        GamepadInputState.JoystickVisual after = state.joystickVisual();
        assertEquals(InputBits.RIGHT, state.mask());
        assertEquals(1, state.activePointerCount());
        assertTrue(after.active());
        assertEquals(before.knobX() - before.centerX(),
                after.knobX() - after.centerX(), .01f);
        assertEquals(before.knobY() - before.centerY(),
                after.knobY() - after.centerY(), .01f);
        assertTrue(state.hasConsistentOwnership());

        state.move(1, x + radius * .5f, y, 3L);
        assertEquals(InputBits.RIGHT, state.mask());
        state.move(1, x - radius * .25f, y, 4L);
        assertEquals(InputBits.LEFT, state.mask());
    }

    @Test public void directionModeReconfigurationCancelsOwner() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        state.down(1, base.centerX(), base.centerY(), 1L);
        state.move(1, base.centerX() + base.width(), base.centerY(), 2L);
        GamepadHitMap dpadMap = GamepadHitMap.fromLayout(
                2340, 1080, 2.75f, 0, 132, 0, 0,
                ControlLayoutV2.recommended(), DirectionControlMode.DPAD, .22f);

        state.reconfigure(dpadMap);

        assertEquals(0, state.mask());
        assertEquals(0, state.activePointerCount());
        assertFalse(state.joystickVisual().active());
    }

    @Test public void ownerUpAndCancelAlwaysClearVisualAndBits() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        state.down(1, base.centerX(), base.centerY(), 1L);
        state.move(1, base.centerX() + base.width(), base.centerY(), 2L);

        state.up(1, 3L);

        assertEquals(0, state.mask());
        assertFalse(state.joystickVisual().active());
        state.down(2, base.centerX(), base.centerY(), 4L);
        state.cancelAll();
        assertEquals(0, state.mask());
        assertEquals(0, state.activePointerCount());
        assertFalse(state.joystickVisual().active());
    }

    @Test public void reusedPointerIdReleasesOldJoystickOwnership() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        GamepadHitMap.Target a = map.target(GamepadHitMap.Control.A);
        state.down(1, base.centerX(), base.centerY(), 1L);
        state.move(1, base.centerX() + base.width(), base.centerY(), 2L);

        assertTrue(state.down(1, a.centerX(), a.centerY(), 3L));

        assertEquals(InputBits.A, state.mask());
        assertFalse(state.joystickVisual().active());
        assertTrue(state.down(2, base.centerX(), base.centerY(), 4L));
        assertEquals(2, state.activePointerCount());
        assertTrue(state.joystickVisual().active());
        assertTrue(state.hasConsistentOwnership());
    }
}
