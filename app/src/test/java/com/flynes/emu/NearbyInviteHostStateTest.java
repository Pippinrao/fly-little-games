package com.flynes.emu;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public final class NearbyInviteHostStateTest {
    @Test public void productStateOnlyDisplaysInvitesAcceptedBySharedBackend() {
        FakeBackend backend = new FakeBackend();
        NearbyInviteHostState state = new NearbyInviteHostState(backend);

        backend.accept = false;
        assertFalse(state.create(1_000L));
        assertFalse(state.active());

        backend.accept = true;
        assertTrue(state.create(2_000L));
        assertTrue(state.active());
        assertTrue(backend.generation == state.generation());
        assertTrue(backend.code.equals(state.code()));

        backend.active = false;
        state.tick(2_100L);
        assertFalse(state.active());
    }

    private static final class FakeBackend implements NearbyInviteHostState.Backend {
        boolean accept;
        boolean active;
        long generation;
        String code = "";

        @Override public boolean publish(long generation, String code, long nowMs) {
            this.generation = generation;
            this.code = code;
            active = accept;
            return accept;
        }

        @Override public boolean regenerate(long generation, String code, long nowMs) {
            return publish(generation, code, nowMs);
        }

        @Override public boolean cancel(long generation) {
            active = false;
            return accept;
        }

        @Override public void tick(long nowMs) {}

        @Override public boolean active(long generation) {
            return active && this.generation == generation;
        }
    }
}
