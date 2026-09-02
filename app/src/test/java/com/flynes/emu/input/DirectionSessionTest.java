package com.flynes.emu.input;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.Random;

public final class DirectionSessionTest {
    private static final int WIDTH = 2340;
    private static final int HEIGHT = 1080;
    private static final int INSET_RIGHT = 132;
    private static final float DENSITY = 2.75f;
    private static final float DEAD_ZONE = .18f;
    private static final int DIRECTIONS = InputBits.UP | InputBits.DOWN
            | InputBits.LEFT | InputBits.RIGHT;

    @Test public void fixedBaseNeverMovesAndKnobClampsAtOneThreeAndFiveRadii() {
        GamepadHitMap map = map(DirectionControlMode.FIXED_JOYSTICK);
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float radius = map.joystickRadius();
        float travel = map.joystickTravelRadius();

        assertTrue(state.down(1, base.centerX(), base.centerY(), 1L));
        for (int multiple : new int[]{1, 3, 5}) {
            state.move(1, base.centerX() + radius * multiple, base.centerY(), multiple + 1L);
            GamepadInputState.JoystickVisual visual = state.joystickVisual();
            assertTrue(visual.active());
            assertTrue(visual.owned());
            assertTrue(visual.activated());
            assertTrue(visual.saturated());
            assertEquals(base.centerX(), visual.centerX(), 0f);
            assertEquals(base.centerY(), visual.centerY(), 0f);
            assertEquals(travel, visual.knobX() - visual.centerX(), .01f);
            assertEquals(0f, visual.knobY() - visual.centerY(), .01f);
            assertTrue(knobDistance(visual) <= travel + .01f);
            assertEquals(InputBits.RIGHT, visual.directionBits());
            assertEquals(InputBits.RIGHT, state.mask() & DIRECTIONS);
        }
        assertEquals(1L, state.directionFeedbackRevision());
    }

    @Test public void activatedDirectionCrossesCenterAndReversesWithoutZeroFrame() {
        GamepadHitMap map = map(DirectionControlMode.FIXED_JOYSTICK);
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float radius = map.joystickRadius();
        assertTrue(state.down(1, base.centerX(), base.centerY(), 1L));

        state.move(1, base.centerX() + radius, base.centerY(), 2L);
        assertEquals(InputBits.RIGHT, state.mask() & DIRECTIONS);
        assertEquals(1L, state.directionFeedbackRevision());

        state.move(1, base.centerX(), base.centerY(), 3L);
        assertEquals(InputBits.RIGHT, state.mask() & DIRECTIONS);
        state.move(1, base.centerX() - radius * .10f, base.centerY(), 4L);
        assertEquals(InputBits.RIGHT, state.mask() & DIRECTIONS);
        assertEquals(1L, state.directionFeedbackRevision());

        state.move(1, base.centerX() - radius * .20f, base.centerY(), 5L);
        assertEquals(InputBits.LEFT, state.mask() & DIRECTIONS);
        assertEquals(2L, state.directionFeedbackRevision());
    }

    @Test public void directionOwnerKeepsRoleAcrossActionZoneWhileButtonsRemainMultitouch() {
        GamepadHitMap map = map(DirectionControlMode.FIXED_JOYSTICK);
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        GamepadHitMap.Target a = map.target(GamepadHitMap.Control.A);
        GamepadHitMap.Target b = map.target(GamepadHitMap.Control.B);
        assertTrue(state.down(1, base.centerX(), base.centerY(), 1L));

        state.move(1, a.centerX(), a.centerY(), 2L);

        int ownedDirection = state.mask() & DIRECTIONS;
        assertTrue(ownedDirection != 0);
        assertEquals(0, state.mask() & (InputBits.A | InputBits.B));
        assertTrue(state.joystickVisual().owned());
        assertEquals(1, state.activePointerCount());

        assertTrue(state.down(2, a.centerX(), a.centerY(), 3L));
        assertTrue(state.down(3, b.centerX(), b.centerY(), 4L));
        assertEquals(ownedDirection | InputBits.A | InputBits.B, state.mask());
        assertEquals(3, state.activePointerCount());

        state.up(2, 5L);
        state.up(3, 6L);
        assertEquals(ownedDirection, state.mask());
        assertTrue(state.joystickVisual().owned());
        assertEquals(1, state.activePointerCount());
    }

    @Test public void fixedCaptureUsesExactSixteenDpBufferAndButtonWinsOverlap() {
        GamepadHitMap map = map(DirectionControlMode.FIXED_JOYSTICK);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float captureRadius = map.joystickRadius() + 16f * DENSITY;
        GamepadInputState state = new GamepadInputState(map);

        assertTrue(state.down(1, base.centerX() + captureRadius, base.centerY(), 1L));
        assertTrue(state.joystickVisual().owned());
        state.up(1, 2L);
        assertFalse(state.down(2, base.centerX() + captureRadius + .1f,
                base.centerY(), 3L));

        ControlLayoutV2 overlap = ControlLayoutV2.recommended().move(
                ControlLayoutV2.Element.A, .10f, .76f);
        GamepadHitMap overlappingMap = GamepadHitMap.fromLayout(
                WIDTH, HEIGHT, DENSITY, 0, INSET_RIGHT, 0, 0,
                overlap, DirectionControlMode.FIXED_JOYSTICK, DEAD_ZONE);
        GamepadInputState overlappingState = new GamepadInputState(overlappingMap);
        GamepadHitMap.Target a = overlappingMap.target(GamepadHitMap.Control.A);

        assertTrue(overlappingState.down(7, a.centerX(), a.centerY(), 4L));
        assertEquals(InputBits.A, overlappingState.mask());
        assertFalse(overlappingState.joystickVisual().owned());
    }

    @Test public void dpadCaptureIncludesCenterAndCompleteExpandedOuterBounds() {
        GamepadHitMap map = map(DirectionControlMode.DPAD);
        GamepadHitMap.Bounds dpad = map.dpadBounds();
        float buffer = 16f * DENSITY;
        GamepadInputState state = new GamepadInputState(map);

        assertTrue(state.down(1, dpad.centerX(), dpad.centerY(), 1L));
        assertEquals(0, state.mask() & DIRECTIONS);
        assertTrue(state.joystickVisual().owned());
        state.up(1, 2L);

        assertTrue(state.down(2, dpad.left - buffer, dpad.top - buffer, 3L));
        assertTrue((state.mask() & DIRECTIONS) != 0);
        state.up(2, 4L);
        assertFalse(state.down(3, dpad.left - buffer - .1f, dpad.centerY(), 5L));
    }

    @Test public void dpadContinuesFarOutsideAndOrbitsAllEightSectorsWithoutNeutralizing() {
        GamepadHitMap map = map(DirectionControlMode.DPAD);
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float distance = map.joystickRadius() * 3f;
        int[] expected = {
                InputBits.RIGHT,
                InputBits.RIGHT | InputBits.DOWN,
                InputBits.DOWN,
                InputBits.LEFT | InputBits.DOWN,
                InputBits.LEFT,
                InputBits.LEFT | InputBits.UP,
                InputBits.UP,
                InputBits.RIGHT | InputBits.UP
        };
        assertTrue(state.down(1, base.centerX(), base.centerY(), 1L));
        assertEquals(0, state.mask() & DIRECTIONS);

        for (int sector = 0; sector < expected.length; sector++) {
            moveAtAngle(state, 1, base, distance, sector * 45f, sector + 2L);
            assertEquals(expected[sector], state.mask() & DIRECTIONS);
            assertEquals(expected[sector], state.joystickVisual().directionBits());
            assertTrue(state.joystickVisual().owned());
            assertEquals(1, state.activePointerCount());
        }

        state.move(1, base.centerX(), base.centerY(), 20L);
        assertEquals(InputBits.RIGHT | InputBits.UP, state.mask() & DIRECTIONS);
        moveAtAngle(state, 1, base, distance, 135f, 21L);
        assertEquals(InputBits.LEFT | InputBits.DOWN, state.mask() & DIRECTIONS);
        assertTrue(state.joystickVisual().owned());
    }

    @Test public void secondDirectionPointerCannotTakeOwnership() {
        for (DirectionControlMode mode : new DirectionControlMode[]{
                DirectionControlMode.FIXED_JOYSTICK, DirectionControlMode.DPAD}) {
            GamepadHitMap map = map(mode);
            GamepadInputState state = new GamepadInputState(map);
            GamepadHitMap.Bounds base = map.dpadBounds();
            float radius = map.joystickRadius();
            assertTrue(state.down(1, base.centerX(), base.centerY(), 1L));
            assertFalse(state.down(2, base.centerX() + radius * .5f,
                    base.centerY(), 2L));
            assertEquals(1, state.activePointerCount());

            state.move(1, base.centerX() + radius, base.centerY(), 3L);
            state.move(2, base.centerX() - radius * 3f, base.centerY(), 4L);
            assertEquals(InputBits.RIGHT, state.mask() & DIRECTIONS);
            assertEquals(1, state.activePointerCount());
            assertTrue(state.hasConsistentOwnership());
        }
    }

    @Test public void exactSectorsUseSevenPointFiveDegreeHysteresis() {
        GamepadHitMap map = map(DirectionControlMode.FIXED_JOYSTICK);
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float distance = map.joystickRadius();
        assertTrue(state.down(1, base.centerX(), base.centerY(), 1L));

        moveAtAngle(state, 1, base, distance, 0f, 2L);
        assertEquals(InputBits.RIGHT, state.mask() & DIRECTIONS);
        assertEquals(1L, state.directionFeedbackRevision());
        moveAtAngle(state, 1, base, distance, 29.9f, 3L);
        assertEquals(InputBits.RIGHT, state.mask() & DIRECTIONS);
        assertEquals(1L, state.directionFeedbackRevision());
        moveAtAngle(state, 1, base, distance, 30.1f, 4L);
        assertEquals(InputBits.RIGHT | InputBits.DOWN, state.mask() & DIRECTIONS);
        assertEquals(2L, state.directionFeedbackRevision());
        moveAtAngle(state, 1, base, distance, 15.1f, 5L);
        assertEquals(InputBits.RIGHT | InputBits.DOWN, state.mask() & DIRECTIONS);
        moveAtAngle(state, 1, base, distance, 14.9f, 6L);
        assertEquals(InputBits.RIGHT, state.mask() & DIRECTIONS);
        assertEquals(3L, state.directionFeedbackRevision());
    }

    @Test public void repeatedMoveAndEquivalentConfigurationAreIdempotent() {
        GamepadHitMap map = map(DirectionControlMode.FIXED_JOYSTICK);
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float x = base.centerX() + map.joystickRadius();
        float y = base.centerY() + map.joystickRadius() * .25f;
        assertTrue(state.down(1, base.centerX(), base.centerY(), 1L));
        state.move(1, x, y, 2L);
        GamepadInputState.JoystickVisual expected = state.joystickVisual();
        int expectedMask = state.mask();
        long expectedRevision = state.directionFeedbackRevision();

        state.move(1, x, y, 3L);
        state.move(1, x, y, 4L);
        state.reconfigure(map);
        state.reconfigure(map(DirectionControlMode.FIXED_JOYSTICK));

        assertVisualEquals(expected, state.joystickVisual());
        assertEquals(expectedMask, state.mask());
        assertEquals(expectedRevision, state.directionFeedbackRevision());
        assertEquals(1, state.activePointerCount());
    }

    @Test public void sameModeReconfigurePreservesOwnerAnchorAndEffectiveVector() {
        GamepadHitMap map = map(DirectionControlMode.FIXED_JOYSTICK);
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float radius = map.joystickRadius();
        assertTrue(state.down(1, base.centerX(), base.centerY(), 1L));
        state.move(1, base.centerX() + radius, base.centerY() - radius * .5f, 2L);
        GamepadInputState.JoystickVisual before = state.joystickVisual();
        int beforeBits = state.mask() & DIRECTIONS;
        long beforeRevision = state.directionFeedbackRevision();
        ControlLayoutV2 relocated = ControlLayoutV2.recommended().move(
                ControlLayoutV2.Element.D_PAD, .25f, .50f);
        GamepadHitMap replacement = GamepadHitMap.fromLayout(
                WIDTH, HEIGHT, DENSITY, 64, INSET_RIGHT, 24, 30,
                relocated, DirectionControlMode.FIXED_JOYSTICK, DEAD_ZONE);

        state.reconfigure(replacement);

        assertTrue(state.joystickVisual().owned());
        assertEquals(1, state.activePointerCount());
        assertEquals(beforeBits, state.mask() & DIRECTIONS);
        assertEquals(beforeRevision, state.directionFeedbackRevision());
        GamepadInputState.JoystickVisual after = state.joystickVisual();
        assertEquals(replacement.dpadBounds().centerX(), after.centerX(), 0f);
        assertEquals(replacement.dpadBounds().centerY(), after.centerY(), 0f);
        assertEquals(before.knobX() - before.centerX(),
                after.knobX() - after.centerX(), .01f);
        assertEquals(before.knobY() - before.centerY(),
                after.knobY() - after.centerY(), .01f);
        assertEquals(before.saturated(), after.saturated());
        assertEquals(before.directionBits(), after.directionBits());
    }

    @Test public void exactModeChangeCancelsDirectionButPreservesButtons() {
        GamepadHitMap joystick = map(DirectionControlMode.JOYSTICK);
        GamepadInputState state = new GamepadInputState(joystick);
        GamepadHitMap.Bounds base = joystick.dpadBounds();
        GamepadHitMap.Target a = joystick.target(GamepadHitMap.Control.A);
        assertTrue(state.down(1, base.centerX(), base.centerY(), 1L));
        state.move(1, base.centerX() + joystick.joystickRadius(), base.centerY(), 2L);
        assertTrue(state.down(2, a.centerX(), a.centerY(), 3L));
        long revision = state.directionFeedbackRevision();

        state.reconfigure(map(DirectionControlMode.FIXED_JOYSTICK));

        assertEquals(InputBits.A, state.mask());
        assertEquals(1, state.activePointerCount());
        assertFalse(state.joystickVisual().owned());
        assertEquals(revision, state.directionFeedbackRevision());
    }

    @Test public void feedbackRevisionOnlyTracksActivationAndStableSectorChanges() {
        GamepadHitMap map = map(DirectionControlMode.FIXED_JOYSTICK);
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float distance = map.joystickRadius();
        assertEquals(0L, state.directionFeedbackRevision());
        assertTrue(state.down(1, base.centerX(), base.centerY(), 1L));
        assertEquals(0L, state.directionFeedbackRevision());

        moveAtAngle(state, 1, base, distance, 0f, 2L);
        assertEquals(1L, state.directionFeedbackRevision());
        moveAtAngle(state, 1, base, distance, 0f, 3L);
        state.move(1, base.centerX(), base.centerY(), 4L);
        assertEquals(1L, state.directionFeedbackRevision());
        moveAtAngle(state, 1, base, distance, 90f, 5L);
        assertEquals(2L, state.directionFeedbackRevision());
        state.up(1, 6L);
        state.cancelAll();
        assertEquals(2L, state.directionFeedbackRevision());

        assertTrue(state.down(2, base.centerX(), base.centerY(), 7L));
        moveAtAngle(state, 2, base, distance, 270f, 8L);
        assertEquals(3L, state.directionFeedbackRevision());
    }

    @Test public void targetedCancelDropsOnlyRequestedRoleWithoutFeedbackSignal() {
        GamepadHitMap map = map(DirectionControlMode.FIXED_JOYSTICK);
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        GamepadHitMap.Target a = map.target(GamepadHitMap.Control.A);
        assertTrue(state.down(1, base.centerX(), base.centerY(), 1L));
        state.move(1, base.centerX() + map.joystickRadius(), base.centerY(), 2L);
        assertTrue(state.down(2, a.centerX(), a.centerY(), 3L));
        long revision = state.directionFeedbackRevision();

        state.cancel(1);

        assertEquals(InputBits.A, state.mask());
        assertFalse(state.joystickVisual().owned());
        assertEquals(1, state.activePointerCount());
        assertEquals(revision, state.directionFeedbackRevision());
        state.cancel(2);
        assertEquals(0, state.mask());
        assertEquals(0, state.activePointerCount());
    }

    @Test public void randomizedVectorsNeverProduceOppositesOrPostActivationNeutral() {
        Random random = new Random(0xD1EC710L);
        for (DirectionControlMode mode : new DirectionControlMode[]{
                DirectionControlMode.FIXED_JOYSTICK, DirectionControlMode.DPAD}) {
            GamepadHitMap map = map(mode);
            GamepadInputState state = new GamepadInputState(map);
            GamepadHitMap.Bounds base = map.dpadBounds();
            float radius = map.joystickRadius();
            assertTrue(state.down(11, base.centerX(), base.centerY(), 1L));
            boolean activated = false;
            for (int sample = 0; sample < 4_000; sample++) {
                float x = base.centerX() + (random.nextFloat() * 10f - 5f) * radius;
                float y = base.centerY() + (random.nextFloat() * 10f - 5f) * radius;
                state.move(11, x, y, sample + 2L);
                int bits = state.mask() & DIRECTIONS;
                assertNoOpposites(bits);
                activated |= bits != 0;
                if (activated) assertTrue(bits != 0);
                assertEquals(bits, state.joystickVisual().directionBits());
                assertTrue(state.joystickVisual().owned());
                assertTrue(knobDistance(state.joystickVisual())
                        <= map.joystickTravelRadius() + .01f);
                assertTrue(state.hasConsistentOwnership());
            }
            state.up(11, 5_000L);
            assertEquals(0, state.mask() & DIRECTIONS);
            assertFalse(state.joystickVisual().owned());
        }
    }

    private static GamepadHitMap map(DirectionControlMode mode) {
        return GamepadHitMap.fromLayout(
                WIDTH, HEIGHT, DENSITY, 0, INSET_RIGHT, 0, 0,
                ControlLayoutV2.recommended(), mode, DEAD_ZONE);
    }

    private static void moveAtAngle(GamepadInputState state, int pointerId,
                                    GamepadHitMap.Bounds base, float distance,
                                    float degrees, long eventTime) {
        double radians = Math.toRadians(degrees);
        state.move(pointerId,
                base.centerX() + (float) Math.cos(radians) * distance,
                base.centerY() + (float) Math.sin(radians) * distance,
                eventTime);
    }

    private static float knobDistance(GamepadInputState.JoystickVisual visual) {
        return (float) Math.hypot(
                visual.knobX() - visual.centerX(),
                visual.knobY() - visual.centerY());
    }

    private static void assertVisualEquals(GamepadInputState.JoystickVisual expected,
                                           GamepadInputState.JoystickVisual actual) {
        assertEquals(expected.active(), actual.active());
        assertEquals(expected.owned(), actual.owned());
        assertEquals(expected.activated(), actual.activated());
        assertEquals(expected.saturated(), actual.saturated());
        assertEquals(expected.centerX(), actual.centerX(), 0f);
        assertEquals(expected.centerY(), actual.centerY(), 0f);
        assertEquals(expected.knobX(), actual.knobX(), 0f);
        assertEquals(expected.knobY(), actual.knobY(), 0f);
        assertEquals(expected.directionBits(), actual.directionBits());
    }

    private static void assertNoOpposites(int bits) {
        assertFalse((bits & InputBits.UP) != 0 && (bits & InputBits.DOWN) != 0);
        assertFalse((bits & InputBits.LEFT) != 0 && (bits & InputBits.RIGHT) != 0);
    }
}
