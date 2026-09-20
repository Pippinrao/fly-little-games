package com.flynes.emu.session;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;

import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;

/** JVM hash-check of the byte-identical flynes_session_v1 schema copy. */
public final class SessionSchemaRegistryTest {
    private static final String FROZEN_HEX =
            "32b52b12b87bf3dea5f747d4b613bb46fc4fc7f3e5f4ff93a10fe4d631726f6b";
    private static final String DOMAIN = "flynes-session-schema-registry-v1";

    @Test
    public void schemaCopyMatchesFrozenRegistryHash() throws Exception {
        byte[] schema = readResource("/flynes_session_v1/flynes_session_v1.schema");
        String published = new String(
                readResource("/flynes_session_v1/schema_registry_hash.txt"),
                StandardCharsets.US_ASCII).trim();

        for (byte value : schema) {
            assertFalse("schema must be LF-normalized", value == (byte) '\r');
        }

        MessageDigest digest = MessageDigest.getInstance("SHA-256");
        digest.update(DOMAIN.getBytes(StandardCharsets.US_ASCII));
        int length = schema.length;
        digest.update(new byte[] {
                (byte) (length >>> 24),
                (byte) (length >>> 16),
                (byte) (length >>> 8),
                (byte) length
        });
        digest.update(schema);
        String recomputed = toHex(digest.digest());

        assertEquals(FROZEN_HEX, published);
        assertEquals(FROZEN_HEX, recomputed);
    }

    private static byte[] readResource(String path) throws Exception {
        try (InputStream in = SessionSchemaRegistryTest.class.getResourceAsStream(path)) {
            assertNotNull(path + " must exist on the test classpath", in);
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            int read;
            while ((read = in.read(buffer)) >= 0) {
                out.write(buffer, 0, read);
            }
            return out.toByteArray();
        }
    }

    private static String toHex(byte[] bytes) {
        StringBuilder builder = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) {
            builder.append(String.format("%02x", value));
        }
        return builder.toString();
    }
}
