package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.UUID;

public final class AndroidUuidSafMapTest {
    @Test
    public void storesPersistableUriByUuidHexAndNeverAcceptsAllZeroUuid() {
        Map<String, String> backing = new LinkedHashMap<>();
        AndroidUuidSafMap map = new AndroidUuidSafMap(backing::get, backing::put, backing::remove);
        byte[] uuid = uuidBytes("11111111-2222-4333-8444-555555555555");

        map.put(uuid, "content://com.android.externalstorage.documents/tree/primary%3Aroms");

        assertEquals("content://com.android.externalstorage.documents/tree/primary%3Aroms",
                map.get(uuid));
        assertEquals("11111111222243338444555555555555",
                backing.keySet().iterator().next());
        assertArrayEquals(uuid, AndroidUuidSafMap.parseHex(backing.keySet().iterator().next()));
        assertNull(map.get(uuidBytes("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee")));
        assertThrows(IllegalArgumentException.class,
                () -> map.put(new byte[16], "content://zero"));
        assertThrows(IllegalArgumentException.class,
                () -> map.put(uuid, "   "));
    }

    @Test
    public void persistsBuiltinUuidOnceAndRemovesSourceRowsWithoutTouchingIt() {
        Map<String, String> backing = new LinkedHashMap<>();
        AndroidUuidSafMap map = new AndroidUuidSafMap(backing::get, backing::put, backing::remove);
        byte[] first = map.builtinUuid();
        byte[] second = map.builtinUuid();
        byte[] user = uuidBytes("01234567-89ab-4cde-8f01-23456789abcd");
        map.put(user, "content://tree/user");

        assertArrayEquals(first, second);
        assertEquals(32, AndroidUuidSafMap.toHex(first).length());
        assertTrue(map.isAssigned(first));
        map.remove(user);
        assertNull(map.get(user));
        assertArrayEquals(first, map.builtinUuid());
    }

    private static byte[] uuidBytes(String value) {
        UUID parsed = UUID.fromString(value);
        byte[] bytes = new byte[16];
        long high = parsed.getMostSignificantBits();
        long low = parsed.getLeastSignificantBits();
        for (int index = 0; index < 8; index++) {
            bytes[index] = (byte) (high >>> (8 * (7 - index)));
            bytes[8 + index] = (byte) (low >>> (8 * (7 - index)));
        }
        return bytes;
    }
}
