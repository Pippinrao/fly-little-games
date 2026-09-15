package com.flynes.emu;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public final class NearbyInviteTickerTest {
    @Test public void deadlineTickRevokesTheDisplayedInvitation() {
        NearbyInviteHostState invite = new NearbyInviteHostState(new AcceptingBackend());
        assertTrue(invite.create(1_000L));
        assertEquals(6, invite.code().length());

        NearbyInviteTicker.tick(invite, 1_000L + NearbyInviteHostState.VALIDITY_MS);

        assertFalse(invite.active());
        assertEquals("", invite.code());
        assertEquals(0L, invite.remainingMs(61_000L));
    }

    private static final class AcceptingBackend implements NearbyInviteHostState.Backend {
        private boolean active;
        private long generation;
        @Override public boolean publish(long generation, String code, long nowMs) {
            this.generation = generation;
            active = true;
            return true;
        }
        @Override public boolean regenerate(long generation, String code, long nowMs) {
            return publish(generation, code, nowMs);
        }
        @Override public boolean cancel(long generation) { active = false; return true; }
        @Override public void tick(long nowMs) {
            if (nowMs >= 1_000L + NearbyInviteHostState.VALIDITY_MS) active = false;
        }
        @Override public boolean active(long generation) {
            return active && this.generation == generation;
        }
    }
}
