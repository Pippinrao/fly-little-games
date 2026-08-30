package com.flynes.emu.save;

import java.util.Arrays;

public record SaveRecord(byte[] state, long savedAt) {
    public SaveRecord {
        state = Arrays.copyOf(state, state.length);
    }

    @Override public byte[] state() {
        return Arrays.copyOf(state, state.length);
    }
}
