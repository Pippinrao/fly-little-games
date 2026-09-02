package com.flynes.emu;

import org.junit.Test;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public class DirectionFeedbackGateTest {
    @Test public void firstRevisionChangeEmits() {
        DirectionFeedbackGate gate = new DirectionFeedbackGate(0L);

        assertTrue(gate.shouldEmit(1L, 100L));
    }

    @Test public void unchangedRevisionDoesNotEmit() {
        DirectionFeedbackGate gate = new DirectionFeedbackGate(0L);

        assertTrue(gate.shouldEmit(1L, 100L));
        assertFalse(gate.shouldEmit(1L, 200L));
    }

    @Test public void suppressedRevisionIsConsumedAndNotReplayed() {
        DirectionFeedbackGate gate = new DirectionFeedbackGate(0L);

        assertTrue(gate.shouldEmit(1L, 100L));
        assertFalse(gate.shouldEmit(2L, 179L));
        assertFalse(gate.shouldEmit(2L, 180L));
        assertTrue(gate.shouldEmit(3L, 180L));
    }

    @Test public void resetConsumesCurrentRevisionAndClearsCooldown() {
        DirectionFeedbackGate gate = new DirectionFeedbackGate(0L);
        assertTrue(gate.shouldEmit(1L, 100L));

        gate.reset(4L);

        assertFalse(gate.shouldEmit(4L, 101L));
        assertTrue(gate.shouldEmit(5L, 101L));
    }

    @Test public void terminalConsumptionDoesNotEmitOrMoveTheCooldownClock() {
        DirectionFeedbackGate gate = new DirectionFeedbackGate(0L);
        assertTrue(gate.shouldEmit(1L, 100L));

        gate.consume(2L);

        assertFalse(gate.shouldEmit(2L, 500L));
        assertTrue(gate.shouldEmit(3L, 180L));
    }
}
