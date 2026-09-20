package com.flynes.emu;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

/**
 * Join submit must survive a failed attempt on the same page: double-click while
 * in flight is one send; after failure the user can edit, retry, or cancel.
 */
public final class NearbyJoinSubmitStateTest {

    @Test public void invalidCodeDoesNotSend() {
        NearbyJoinSubmitState state = new NearbyJoinSubmitState();
        assertFalse(state.submit("12345"));
        assertFalse(state.submit("1234567"));
        assertFalse(state.submit("12A456"));
        assertEquals(0, state.sendCount());
        assertFalse(state.cancelVisible());
        assertTrue(state.submitEnabled("123456"));
    }

    @Test public void inFlightDoubleSubmitIsOneSend() {
        NearbyJoinSubmitState state = new NearbyJoinSubmitState();
        assertTrue(state.submit("123456"));
        assertTrue(state.inFlight());
        assertFalse(state.submitEnabled("123456"));
        assertFalse(state.submit("123456"));
        assertEquals(1, state.sendCount());
        assertTrue(state.cancelVisible());
        assertTrue(state.inputLocked());
    }

    @Test public void failureRestoresRetryAndKeepsCancel() {
        NearbyJoinSubmitState state = new NearbyJoinSubmitState();
        assertTrue(state.submit("123456"));
        state.onFailure();
        assertFalse(state.inFlight());
        assertTrue(state.submitEnabled("123456"));
        assertTrue(state.cancelVisible());
        assertFalse(state.inputLocked());
        assertTrue(state.submit("654321"));
        assertEquals(2, state.sendCount());
    }

    @Test public void cancelClearsFlightAndDoesNotSend() {
        NearbyJoinSubmitState state = new NearbyJoinSubmitState();
        assertTrue(state.submit("123456"));
        state.cancel();
        assertFalse(state.inFlight());
        assertFalse(state.cancelVisible());
        assertTrue(state.submitEnabled("123456"));
        assertEquals(1, state.sendCount());
    }
}
