package com.flynes.emu.catalog.scan;

import com.flynes.emu.catalog.RomHashes;

import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;
import java.util.zip.CRC32;

/** Package-private production hash path shared by scanner and parity tests. */
final class RomContentHasher {
    private RomContentHasher() {
    }

    static RomHashes hashes(byte[] payload, String physicalSha256) {
        CRC32 crc32 = new CRC32();
        crc32.update(payload);
        return new RomHashes(
                digest("SHA-1", payload),
                digest("SHA-256", payload),
                physicalSha256,
                String.format(Locale.ROOT, "%08X", crc32.getValue()));
    }

    static String sha256(byte[] bytes) {
        return digest("SHA-256", bytes);
    }

    private static String digest(String algorithm, byte[] bytes) {
        try {
            StringBuilder hex = new StringBuilder();
            for (byte value : MessageDigest.getInstance(algorithm).digest(bytes)) {
                hex.append(String.format(Locale.ROOT, "%02X", value & 0xFF));
            }
            return hex.toString();
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException(algorithm + " is required", impossible);
        }
    }
}
