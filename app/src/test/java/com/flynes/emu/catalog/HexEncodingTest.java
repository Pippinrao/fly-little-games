package com.flynes.emu.catalog;

import org.junit.Test;
import java.util.Locale;
import static org.junit.Assert.assertEquals;

public final class HexEncodingTest {
    @Test public void preservesEveryByteAndLeadingZerosForExistingPersistentIds() {
        byte[] bytes = new byte[256];
        StringBuilder expected = new StringBuilder();
        for (int i = 0; i < bytes.length; ++i) {
            bytes[i] = (byte) i;
            expected.append(String.format(Locale.ROOT, "%02X", i));
        }
        assertEquals(expected.toString(), HexEncoding.upper(bytes));
        assertEquals(expected.toString().toLowerCase(Locale.ROOT), HexEncoding.lower(bytes));
        assertEquals("", HexEncoding.upper(new byte[0]));
    }
}
