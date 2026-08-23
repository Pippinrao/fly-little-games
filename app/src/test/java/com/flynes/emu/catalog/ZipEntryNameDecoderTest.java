package com.flynes.emu.catalog;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.zip.CRC32;

public final class ZipEntryNameDecoderTest {
    @Test
    public void efsAlwaysUsesStrictUtf8() {
        byte[] rawName = "目录/魂斗罗.nes".getBytes(StandardCharsets.UTF_8);

        ZipEntryNameDecoder.DecodedName decoded = ZipEntryNameDecoder.decode(
                rawName, 0x0800, new byte[0], ZipNameEncoding.GB18030);

        assertEquals("目录/魂斗罗.nes", decoded.displayPath());
        assertEquals(ZipNameEncoding.UTF8_EFS, decoded.encoding());
        assertFalse(decoded.unicodePathRejected());
        assertThrows(IllegalArgumentException.class, () -> ZipEntryNameDecoder.decode(
                new byte[]{(byte) 0xC3, 0x28},
                0x0800,
                new byte[0],
                ZipNameEncoding.CP437));
    }

    @Test
    public void verifiedUnicodePathWinsAndBadCrcFallsBack() {
        byte[] rawName = "game.nes".getBytes(StandardCharsets.US_ASCII);
        byte[] valid = unicodePathExtra(rawName, "魂斗罗.nes", false);
        byte[] invalid = unicodePathExtra(rawName, "魂斗罗.nes", true);

        ZipEntryNameDecoder.DecodedName unicode = ZipEntryNameDecoder.decode(
                rawName, 0, valid, ZipNameEncoding.CP437);
        ZipEntryNameDecoder.DecodedName fallback = ZipEntryNameDecoder.decode(
                rawName, 0, invalid, ZipNameEncoding.CP437);

        assertEquals("魂斗罗.nes", unicode.displayPath());
        assertEquals(ZipNameEncoding.UNICODE_PATH, unicode.encoding());
        assertFalse(unicode.unicodePathRejected());
        assertEquals("game.nes", fallback.displayPath());
        assertEquals(ZipNameEncoding.CP437, fallback.encoding());
        assertTrue(fallback.unicodePathRejected());
    }

    @Test
    public void nonEfsFallbackIsExplicitCp437OrGb18030() {
        ZipEntryNameDecoder.DecodedName cp437 = ZipEntryNameDecoder.decode(
                "Grüße.nes".getBytes(Charset.forName("IBM437")),
                0,
                new byte[0],
                ZipNameEncoding.CP437);
        ZipEntryNameDecoder.DecodedName gb18030 = ZipEntryNameDecoder.decode(
                "魂斗罗.nes".getBytes(Charset.forName("GB18030")),
                0,
                new byte[0],
                ZipNameEncoding.GB18030);

        assertEquals("Grüße.nes", cp437.displayPath());
        assertEquals("魂斗罗.nes", gb18030.displayPath());
        assertThrows(IllegalArgumentException.class, () -> ZipEntryNameDecoder.decode(
                new byte[]{'a'}, 0, new byte[0], ZipNameEncoding.UTF8_EFS));
    }

    private static byte[] unicodePathExtra(
            byte[] rawName, String unicodeName, boolean corruptCrc) {
        byte[] utf8 = unicodeName.getBytes(StandardCharsets.UTF_8);
        CRC32 crc = new CRC32();
        crc.update(rawName);
        long value = corruptCrc ? crc.getValue() ^ 1L : crc.getValue();
        byte[] extra = new byte[9 + utf8.length];
        extra[0] = 0x75;
        extra[1] = 0x70;
        extra[2] = (byte) (5 + utf8.length);
        extra[4] = 1;
        extra[5] = (byte) value;
        extra[6] = (byte) (value >>> 8);
        extra[7] = (byte) (value >>> 16);
        extra[8] = (byte) (value >>> 24);
        System.arraycopy(utf8, 0, extra, 9, utf8.length);
        return extra;
    }
}
