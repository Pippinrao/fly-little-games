package com.flynes.emu.catalog.scan;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.zip.CRC32;
import java.util.zip.Deflater;

public final class BoundedZipDescriptorTest {
    private static final byte[] PAYLOAD = "descriptor-payload".getBytes(StandardCharsets.UTF_8);

    @Test
    public void acceptsSignedAndUnsignedDescriptorsForStoredAndDeflatedEntries() throws Exception {
        for (int method : new int[]{0, 8}) {
            for (boolean signature : new boolean[]{false, true}) {
                BoundedZipArchive.Archive archive = BoundedZipArchive.fromBytes(
                        zip(method, signature, true), ScanLimits.defaults());
                assertEquals(1, archive.entries().size());
                assertArrayEquals(PAYLOAD, archive.entries().get(0).readPayload());
            }
        }
    }

    @Test
    public void rejectsMissingTruncatedAndMismatchedDescriptors() throws Exception {
        assertInvalid(zip(0, true, false));
        byte[] truncated = zip(0, true, true);
        int descriptor = descriptorOffset(0);
        byte[] shiftedCentral = removeRange(truncated, descriptor + 7, 9);
        patchCentralOffsetInEocd(shiftedCentral, descriptor + 7);
        assertInvalid(shiftedCentral);

        for (int method : new int[]{0, 8}) {
            for (boolean signature : new boolean[]{false, true}) {
                for (int field = 0; field < 3; field++) {
                    byte[] mismatched = zip(method, signature, true);
                    int valueOffset = descriptorOffset(method)
                            + (signature ? 4 : 0) + field * 4;
                    mismatched[valueOffset] ^= 0x01;
                    assertInvalid(mismatched);
                }
            }
        }
    }

    private static void assertInvalid(byte[] zip) {
        BoundedZipArchive.ArchiveException failure = assertThrows(
                BoundedZipArchive.ArchiveException.class,
                () -> BoundedZipArchive.fromBytes(zip, ScanLimits.defaults()));
        assertEquals(BoundedZipArchive.Code.INVALID_ZIP, failure.code());
    }

    private static int descriptorOffset(int method) {
        byte[] compressed = method == 0 ? PAYLOAD : deflate(PAYLOAD);
        return 30 + "game.nes".length() + compressed.length;
    }

    private static byte[] zip(int method, boolean signature, boolean descriptorPresent) {
        byte[] name = "game.nes".getBytes(StandardCharsets.UTF_8);
        byte[] compressed = method == 0 ? PAYLOAD : deflate(PAYLOAD);
        CRC32 crc = new CRC32();
        crc.update(PAYLOAD);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        u32(out, 0x04034b50L); u16(out, 20); u16(out, 8); u16(out, method);
        u16(out, 0); u16(out, 0); u32(out, 0); u32(out, 0); u32(out, 0);
        u16(out, name.length); u16(out, 0); bytes(out, name); bytes(out, compressed);
        if (descriptorPresent) {
            if (signature) u32(out, 0x08074b50L);
            u32(out, crc.getValue()); u32(out, compressed.length); u32(out, PAYLOAD.length);
        }
        int centralOffset = out.size();
        u32(out, 0x02014b50L); u16(out, 20); u16(out, 20); u16(out, 8); u16(out, method);
        u16(out, 0); u16(out, 0); u32(out, crc.getValue()); u32(out, compressed.length);
        u32(out, PAYLOAD.length); u16(out, name.length); u16(out, 0); u16(out, 0);
        u16(out, 0); u16(out, 0); u32(out, 0); u32(out, 0); bytes(out, name);
        int centralSize = out.size() - centralOffset;
        u32(out, 0x06054b50L); u16(out, 0); u16(out, 0); u16(out, 1); u16(out, 1);
        u32(out, centralSize); u32(out, centralOffset); u16(out, 0);
        return out.toByteArray();
    }

    private static byte[] deflate(byte[] value) {
        Deflater deflater = new Deflater(Deflater.DEFAULT_COMPRESSION, true);
        deflater.setInput(value); deflater.finish();
        byte[] result = new byte[128];
        int length = deflater.deflate(result); deflater.end();
        byte[] exact = new byte[length];
        System.arraycopy(result, 0, exact, 0, length);
        return exact;
    }

    private static byte[] removeRange(byte[] source, int offset, int count) {
        byte[] result = new byte[source.length - count];
        System.arraycopy(source, 0, result, 0, offset);
        System.arraycopy(source, offset + count, result, offset, source.length - offset - count);
        return result;
    }

    private static void patchCentralOffsetInEocd(byte[] zip, int centralOffset) {
        int eocd = zip.length - 22;
        putU32(zip, eocd + 16, centralOffset);
    }

    private static void putU32(byte[] value, int offset, long item) {
        for (int index = 0; index < 4; index++) value[offset + index] = (byte) (item >>> (8 * index));
    }

    private static void u16(ByteArrayOutputStream out, int value) {
        out.write(value & 0xff); out.write((value >>> 8) & 0xff);
    }

    private static void u32(ByteArrayOutputStream out, long value) {
        u16(out, (int) value); u16(out, (int) (value >>> 16));
    }

    private static void bytes(ByteArrayOutputStream out, byte[] value) {
        out.write(value, 0, value.length);
    }
}
