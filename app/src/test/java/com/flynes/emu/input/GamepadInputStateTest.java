package com.flynes.emu.input;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.Random;

public final class GamepadInputStateTest {
    private final GamepadHitMap map = GamepadHitMap.standard(2340, 1080, 2.75f, 0, 132, 0, 0);

    @Test public void bRollToAProducesCombinationWithoutEarlyBRelease() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Target b = map.target(GamepadHitMap.Control.B);
        GamepadHitMap.Target a = map.target(GamepadHitMap.Control.A);
        state.down(7, b.centerX(), b.centerY(), 100);
        assertEquals(InputBits.B, state.mask());
        state.move(7, a.centerX(), a.centerY(), 110);
        assertEquals(InputBits.B | InputBits.A, state.mask());
        state.up(7, 140);
        assertEquals(0, state.mask());
    }

    @Test public void selectAndStartUseTrueHoldUntilUpOrCancel() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Target select = map.target(GamepadHitMap.Control.SELECT);
        GamepadHitMap.Target start = map.target(GamepadHitMap.Control.START);
        state.down(1, select.centerX(), select.centerY(), 1);
        state.down(2, start.centerX(), start.centerY(), 2);
        assertEquals(InputBits.SELECT | InputBits.START, state.mask());
        state.move(1, select.centerX(), select.centerY(), 500);
        assertEquals(InputBits.SELECT | InputBits.START, state.mask());
        state.up(1, 501);
        assertEquals(InputBits.START, state.mask());
        state.cancelAll();
        assertEquals(0, state.mask());
        assertEquals(0, state.activePointerCount());
    }

    @Test public void opposingDirectionsFromDifferentPointersNeverCoexist() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds d = map.dpadBounds();
        state.down(1, d.centerX(), d.top + 4, 1);
        state.down(2, d.centerX(), d.bottom - 4, 2);
        assertNoOpposites(state.mask());
        state.move(1, d.left + 4, d.centerY(), 3);
        state.move(2, d.right - 4, d.centerY(), 4);
        assertNoOpposites(state.mask());
    }

    @Test public void fixedSeedTenThousandPointerSequencesAlwaysTerminateCleanly() {
        Random random = new Random(0xF17E5L);
        for (int sequence = 0; sequence < 10_000; sequence++) {
            GamepadInputState state = new GamepadInputState(map);
            for (int event = 0; event < 24; event++) {
                int id = random.nextInt(6);
                float x = random.nextFloat() * 2340f;
                float y = random.nextFloat() * 1080f;
                switch (random.nextInt(3)) {
                    case 0 -> state.down(id, x, y, event);
                    case 1 -> state.move(id, x, y, event);
                    default -> state.up(id, event);
                }
                assertNoOpposites(state.mask());
            }
            state.cancelAll();
            assertEquals(0, state.mask());
            assertEquals(0, state.activePointerCount());
            assertTrue(state.hasConsistentOwnership());
        }
    }

    private static void assertNoOpposites(int mask) {
        assertFalse((mask & InputBits.UP) != 0 && (mask & InputBits.DOWN) != 0);
        assertFalse((mask & InputBits.LEFT) != 0 && (mask & InputBits.RIGHT) != 0);
    }
}
