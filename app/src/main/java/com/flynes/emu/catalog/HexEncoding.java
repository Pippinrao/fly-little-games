package com.flynes.emu.catalog;

/** Allocation-bounded encoding for hashes and UUIDs in large catalog snapshots. */
public final class HexEncoding {
    private HexEncoding() { }

    public static String upper(byte[] bytes) { return encode(bytes, "0123456789ABCDEF"); }
    public static String lower(byte[] bytes) { return encode(bytes, "0123456789abcdef"); }

    private static String encode(byte[] bytes, String digits) {
        char[] text = new char[bytes.length * 2];
        for (int i = 0; i < bytes.length; ++i) {
            int value = bytes[i] & 0xff;
            text[i * 2] = digits.charAt(value >>> 4);
            text[i * 2 + 1] = digits.charAt(value & 15);
        }
        return new String(text);
    }
}
