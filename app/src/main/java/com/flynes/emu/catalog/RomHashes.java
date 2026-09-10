package com.flynes.emu.catalog;

import com.flynes.emu.data.RomIdentity;

/** Hashes of an exact ROM payload and of the physical package that contains it. */
public record RomHashes(
        String payloadSha1,
        String payloadSha256,
        String physicalPackageSha256,
        String crc32) {

    public RomHashes {
        payloadSha1 = normalizedHex(payloadSha1, 40, "payload SHA-1");
        payloadSha256 = normalizedHex(payloadSha256, 64, "payload SHA-256");
        physicalPackageSha256 = normalizedHex(
                physicalPackageSha256, 64, "physical package SHA-256");
        crc32 = normalizedHex(crc32, 8, "payload CRC32");
    }

    public RomIdentity romIdentity() {
        return new RomIdentity(payloadSha1);
    }

    public static String normalizedSha256(String value, String name) {
        return normalizedHex(value, 64, name);
    }

    private static String normalizedHex(String value, int length, String name) {
        if (value == null || value.length() != length) {
            throw new IllegalArgumentException(name + " must be " + length + " hex characters");
        }
        for (int i = 0; i < value.length(); i++) {
            // Character.digit accepts non-ASCII Unicode digits; persisted hashes do not.
            char character = value.charAt(i);
            boolean decimal = character >= '0' && character <= '9';
            boolean lower = character >= 'a' && character <= 'f';
            boolean upper = character >= 'A' && character <= 'F';
            if (!decimal && !lower && !upper) {
                throw new IllegalArgumentException(name + " must be hexadecimal");
            }
        }
        StringBuilder normalized = new StringBuilder(length);
        for (int i = 0; i < value.length(); i++) {
            char character = value.charAt(i);
            normalized.append(character >= 'a' && character <= 'f'
                    ? (char) (character - ('a' - 'A'))
                    : character);
        }
        return normalized.toString();
    }
}
