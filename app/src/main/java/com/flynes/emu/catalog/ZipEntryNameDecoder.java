package com.flynes.emu.catalog;

import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.Charset;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Objects;
import java.util.zip.CRC32;

/**
 * Locale-independent ZIP display-name decoder for scanners. Exact launch identity remains the
 * raw name plus local-header offset and does not depend on this decoded text.
 */
public final class ZipEntryNameDecoder {
    private static final int EFS_FLAG = 0x0800;
    private static final int UNICODE_PATH_TAG = 0x7075;

    private ZipEntryNameDecoder() {
    }

    public static DecodedName decode(
            byte[] rawName,
            int generalPurposeFlags,
            byte[] centralExtra,
            ZipNameEncoding legacyFallback) {
        Objects.requireNonNull(rawName, "raw name");
        Objects.requireNonNull(centralExtra, "central extra");
        Objects.requireNonNull(legacyFallback, "legacy fallback");
        if (rawName.length == 0 || rawName.length > 0xFFFF) {
            throw new IllegalArgumentException("raw ZIP name length is invalid");
        }
        if ((generalPurposeFlags & EFS_FLAG) != 0) {
            return new DecodedName(
                    decodeStrict(rawName, StandardCharsets.UTF_8),
                    ZipNameEncoding.UTF8_EFS,
                    false);
        }

        UnicodePath unicodePath = findUnicodePath(rawName, centralExtra);
        if (unicodePath.displayPath != null && !unicodePath.rejected) {
            return new DecodedName(
                    unicodePath.displayPath, ZipNameEncoding.UNICODE_PATH, false);
        }

        Charset fallbackCharset = switch (legacyFallback) {
            case CP437 -> Charset.forName("IBM437");
            case GB18030 -> Charset.forName("GB18030");
            default -> throw new IllegalArgumentException(
                    "non-EFS ZIP names require an explicit CP437 or GB18030 fallback");
        };
        return new DecodedName(
                decodeStrict(rawName, fallbackCharset),
                legacyFallback,
                unicodePath.rejected);
    }

    private static UnicodePath findUnicodePath(byte[] rawName, byte[] extra) {
        String displayPath = null;
        boolean rejected = false;
        int cursor = 0;
        while (cursor < extra.length) {
            if (extra.length - cursor < 4) {
                return new UnicodePath(null, true);
            }
            int tag = unsignedShort(extra, cursor);
            int length = unsignedShort(extra, cursor + 2);
            cursor += 4;
            if (length > extra.length - cursor) {
                return new UnicodePath(null, true);
            }
            if (tag == UNICODE_PATH_TAG) {
                if (displayPath != null || length < 5 || extra[cursor] != 1) {
                    rejected = true;
                } else {
                    CRC32 crc = new CRC32();
                    crc.update(rawName);
                    long declaredCrc = unsignedInt(extra, cursor + 1);
                    if (declaredCrc != crc.getValue()) {
                        rejected = true;
                    } else {
                        try {
                            String candidate = decodeStrict(
                                    Arrays.copyOfRange(
                                            extra, cursor + 5, cursor + length),
                                    StandardCharsets.UTF_8);
                            if (candidate.isBlank()) {
                                rejected = true;
                            } else {
                                displayPath = candidate;
                            }
                        } catch (IllegalArgumentException malformedUtf8) {
                            rejected = true;
                        }
                    }
                }
            }
            cursor += length;
        }
        return new UnicodePath(displayPath, rejected);
    }

    private static String decodeStrict(byte[] bytes, Charset charset) {
        try {
            return charset.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes))
                    .toString();
        } catch (CharacterCodingException failure) {
            throw new IllegalArgumentException("ZIP entry name has invalid encoding", failure);
        }
    }

    private static int unsignedShort(byte[] bytes, int offset) {
        return (bytes[offset] & 0xFF) | ((bytes[offset + 1] & 0xFF) << 8);
    }

    private static long unsignedInt(byte[] bytes, int offset) {
        return (bytes[offset] & 0xFFL)
                | ((bytes[offset + 1] & 0xFFL) << 8)
                | ((bytes[offset + 2] & 0xFFL) << 16)
                | ((bytes[offset + 3] & 0xFFL) << 24);
    }

    private record UnicodePath(String displayPath, boolean rejected) {
    }

    public record DecodedName(
            String displayPath,
            ZipNameEncoding encoding,
            boolean unicodePathRejected) {

        public DecodedName {
            displayPath = DomainValidation.requireNonBlank(displayPath, "ZIP display path");
            encoding = DomainValidation.requireNonNull(encoding, "ZIP name encoding");
        }
    }
}
