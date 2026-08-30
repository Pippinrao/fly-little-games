package com.flynes.emu.save;

import static java.nio.charset.StandardCharsets.US_ASCII;
import static java.nio.charset.StandardCharsets.UTF_8;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import com.flynes.emu.data.RomIdentity;

import org.junit.Test;

public final class StateHeaderReaderTest {
    private static final String SHA1 = "77C42676DB38D384C1D6B00090ADBC820BF70AB0";

    @Test
    public void extractsSha1AtOffset28() {
        byte[] state = validEmptyState();
        assertEquals(new RomIdentity(SHA1), StateHeaderReader.identity(state));
    }

    @Test
    public void rejectsTruncatedOrUnknownState() {
        assertThrows(IllegalArgumentException.class,
                () -> StateHeaderReader.identity("FLYNST1\0".getBytes(UTF_8)));
        assertThrows(IllegalArgumentException.class,
                () -> StateHeaderReader.identity(new byte[81]));
    }

    private static byte[] validEmptyState() {
        byte[] state = new byte[81];
        System.arraycopy("FLYNST1\0".getBytes(UTF_8), 0, state, 0, 8);
        state[8] = 1;
        byte[] sha = SHA1.getBytes(US_ASCII);
        System.arraycopy(sha, 0, state, 28, sha.length);
        return state;
    }
}
