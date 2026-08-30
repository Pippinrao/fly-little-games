package com.flynes.emu.save;

import com.flynes.emu.data.RomIdentity;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.zip.CRC32;

public final class StateHeaderReader {
    public static final int HEADER_SIZE = 81;
    private static final byte[] MAGIC = {'F', 'L', 'Y', 'N', 'S', 'T', '1', 0};

    private StateHeaderReader() { }

    public static RomIdentity identity(byte[] state) {
        validate(state);
        String sha1 = new String(state, 28, 40, StandardCharsets.US_ASCII);
        return new RomIdentity(sha1);
    }

    public static void validate(byte[] state) {
        if (state == null || state.length < HEADER_SIZE) fail("state header is truncated");
        if (!Arrays.equals(MAGIC, Arrays.copyOfRange(state, 0, MAGIC.length))) {
            fail("state magic is invalid");
        }
        if (u32(state, 8) != 1L) fail("state version is unsupported");
        if (state[68] != 0) fail("state SHA-1 is not terminated");
        String sha1 = new String(state, 28, 40, StandardCharsets.US_ASCII);
        if (!sha1.matches("[0-9A-Fa-f]{40}")) fail("state SHA-1 is invalid");
        long length = u64(state, 69);
        if (length < 0L || length > state.length - HEADER_SIZE) fail("payload length is invalid");
        CRC32 crc = new CRC32();
        crc.update(state, HEADER_SIZE, (int) length);
        if (crc.getValue() != u32(state, 77)) fail("payload CRC32 is invalid");
    }

    private static long u32(byte[] data, int offset) {
        return (data[offset] & 0xFFL)
                | ((data[offset + 1] & 0xFFL) << 8)
                | ((data[offset + 2] & 0xFFL) << 16)
                | ((data[offset + 3] & 0xFFL) << 24);
    }

    private static long u64(byte[] data, int offset) {
        long value = 0L;
        for (int i = 7; i >= 0; i--) value = (value << 8) | (data[offset + i] & 0xFFL);
        return value;
    }

    private static void fail(String message) {
        throw new IllegalArgumentException(message);
    }
}
