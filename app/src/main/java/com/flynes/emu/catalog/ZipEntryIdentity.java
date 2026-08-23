package com.flynes.emu.catalog;

import java.util.Locale;

/** Exact, locale-independent identity of one ZIP entry. */
public record ZipEntryIdentity(
        String rawNameHex,
        int localHeaderOffset) {

    public ZipEntryIdentity {
        rawNameHex = DomainValidation.requireNonBlank(rawNameHex, "raw ZIP entry name");
        if (rawNameHex.length() > 0xFFFF * 2) {
            throw new IllegalArgumentException("raw ZIP entry name exceeds the ZIP field limit");
        }
        if ((rawNameHex.length() & 1) != 0) {
            throw new IllegalArgumentException("raw ZIP entry name must contain whole bytes");
        }
        for (int i = 0; i < rawNameHex.length(); i++) {
            if (Character.digit(rawNameHex.charAt(i), 16) < 0) {
                throw new IllegalArgumentException("raw ZIP entry name must be hexadecimal");
            }
        }
        rawNameHex = rawNameHex.toUpperCase(Locale.ROOT);
        if (localHeaderOffset < 0) {
            throw new IllegalArgumentException("ZIP local-header offset must not be negative");
        }
    }

    public static ZipEntryIdentity fromRawName(byte[] rawName, int localHeaderOffset) {
        DomainValidation.requireNonNull(rawName, "raw ZIP entry name");
        if (rawName.length == 0) {
            throw new IllegalArgumentException("raw ZIP entry name must not be empty");
        }
        if (rawName.length > 0xFFFF) {
            throw new IllegalArgumentException("raw ZIP entry name exceeds the ZIP field limit");
        }
        StringBuilder hex = new StringBuilder(rawName.length * 2);
        for (byte value : rawName) {
            hex.append(Character.forDigit((value >>> 4) & 0x0F, 16));
            hex.append(Character.forDigit(value & 0x0F, 16));
        }
        return new ZipEntryIdentity(hex.toString(), localHeaderOffset);
    }

    public byte[] rawNameBytes() {
        byte[] raw = new byte[rawNameHex.length() / 2];
        for (int i = 0; i < raw.length; i++) {
            raw[i] = (byte) ((Character.digit(rawNameHex.charAt(i * 2), 16) << 4)
                    | Character.digit(rawNameHex.charAt(i * 2 + 1), 16));
        }
        return raw;
    }
}
