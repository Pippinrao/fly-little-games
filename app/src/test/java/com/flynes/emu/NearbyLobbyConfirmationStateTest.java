package com.flynes.emu;

import org.junit.Test;

import static org.junit.Assert.*;

public final class NearbyLobbyConfirmationStateTest {
    private NearbySessionOwner.Snapshot snapshot(byte[] id, long revision, int local, int peer) {
        return new NearbySessionOwner.Snapshot(2, 8, 1, local, peer, "", id, revision,
                0L, 0, 0, false);
    }

    private boolean enabled(NearbySessionOwner.Snapshot snapshot) {
        return snapshot.canConfirmGameConfig();
    }

    @Test public void emptyIdCannotBeConfirmedEvenWhenBothFlagsAreSet() throws Exception {
        assertFalse(enabled(snapshot(new byte[32], 1, 0, 0)));
        assertFalse(enabled(snapshot(new byte[32], 1, 1, 1)));
    }

    @Test public void pendingConfigNeedsOnlyLocalConsentToEnableButton() throws Exception {
        byte[] id = new byte[32];
        id[31] = 42;
        assertFalse(enabled(snapshot(id, 0, 0, 0)));
        assertTrue(enabled(snapshot(id, 7, 0, 0)));
        assertTrue(enabled(snapshot(id, 7, 0, 1)));
        assertFalse(enabled(snapshot(id, 7, 1, 0)));
        assertFalse(enabled(snapshot(id, 7, 1, 1)));
    }

    @Test public void pendingIdIsCopiedAtSnapshotAndReadBoundaries() throws Exception {
        byte[] id = new byte[32];
        id[0] = 42;
        NearbySessionOwner.Snapshot snap = snapshot(id, 7, 0, 0);
        id[0] = 0;
        byte[] first = snap.pendingConfigId();
        assertEquals(42, first[0]);
        first[0] = 0;
        assertEquals(42, snap.pendingConfigId()[0]);
    }
}
