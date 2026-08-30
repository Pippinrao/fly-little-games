package com.flynes.emu.settings;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import org.junit.Test;

public final class SettingsBatchTest {
    @Test public void exposesImmutableTypedValuesAndRemovals() {
        SettingsBatch batch = new SettingsBatch.Builder()
                .putString("name", "value")
                .putInt("generation", 7)
                .putBoolean("enabled", true)
                .remove("stale")
                .build();

        assertEquals("value", batch.strings().get("name"));
        assertEquals(Integer.valueOf(7), batch.integers().get("generation"));
        assertEquals(Boolean.TRUE, batch.booleans().get("enabled"));
        assertEquals(true, batch.removals().contains("stale"));
        assertThrows(UnsupportedOperationException.class,
                () -> batch.strings().put("other", "broken"));
    }

    @Test public void rejectsNullDuplicateAndCrossTypeKeys() {
        assertThrows(NullPointerException.class,
                () -> new SettingsBatch.Builder().putString(null, "value"));
        assertThrows(NullPointerException.class,
                () -> new SettingsBatch.Builder().putString("key", null));
        assertThrows(IllegalArgumentException.class, () -> new SettingsBatch.Builder()
                .putString("same", "first").putString("same", "second"));
        assertThrows(IllegalArgumentException.class, () -> new SettingsBatch.Builder()
                .putString("same", "value").putInt("same", 1));
        assertThrows(IllegalArgumentException.class, () -> new SettingsBatch.Builder()
                .remove("same").putBoolean("same", true));
    }
}
