package com.flynes.emu;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public final class BuildSmokeTest {
    private record Java17Record(int value) {}

    @Test
    public void java17IsActive() {
        assertEquals(17, new Java17Record(17).value());
    }
}
