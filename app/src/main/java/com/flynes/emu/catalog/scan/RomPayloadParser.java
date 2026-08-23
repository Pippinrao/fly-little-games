package com.flynes.emu.catalog.scan;

import com.flynes.emu.catalog.CompatibilityDecision;
import com.flynes.emu.catalog.CompatibilityReason;
import com.flynes.emu.catalog.CompatibilityState;
import com.flynes.emu.catalog.RomAnalysis;
import com.flynes.emu.catalog.RomFormat;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

final class RomPayloadParser {
    private static final int INES_HEADER_BYTES = 16;
    private static final int TRAINER_BYTES = 512;
    private static final int FDS_HEADER_BYTES = 16;
    private static final int FDS_SIDE_BYTES = 65_500;
    private static final byte[] FDS_SIDE_SIGNATURE = new byte[]{
            1, '*', 'N', 'I', 'N', 'T', 'E', 'N', 'D', 'O', '-', 'H', 'V', 'C', '*'};

    private RomPayloadParser() {
    }

    static Parsed parse(byte[] payload) {
        if (startsWith(payload, new byte[]{'N', 'E', 'S', 0x1A})) {
            return parseNes(payload);
        }
        if (startsWith(payload, new byte[]{'F', 'D', 'S', 0x1A})) {
            return parseHeaderedFds(payload);
        }
        if (looksLikeHeaderlessFds(payload)) {
            return parseHeaderlessFds(payload);
        }
        if (startsWith(payload, new byte[]{'U', 'N', 'I', 'F'})) {
            return parseUnif(payload);
        }
        return null;
    }

    private static Parsed parseNes(byte[] payload) {
        if (payload.length < INES_HEADER_BYTES) {
            return invalid(
                    RomFormat.INES,
                    CompatibilityReason.NES_HEADER_INVALID,
                    RomAnalysis.basic(payload.length));
        }
        int flags6 = unsigned(payload[6]);
        int flags7 = unsigned(payload[7]);
        boolean nes2 = (flags7 & 0x0C) == 0x08;
        RomFormat format = nes2 ? RomFormat.NES2 : RomFormat.INES;
        boolean trainer = (flags6 & 0x04) != 0;
        boolean battery = (flags6 & 0x02) != 0;
        int mapper = (flags6 >>> 4) | (flags7 & 0xF0);
        int submapper = 0;
        long prgBytes;
        long chrBytes;
        try {
            if (nes2) {
                int byte8 = unsigned(payload[8]);
                int byte9 = unsigned(payload[9]);
                mapper |= (byte8 & 0x0F) << 8;
                submapper = byte8 >>> 4;
                prgBytes = nes2Size(unsigned(payload[4]), byte9 & 0x0F, 16_384L);
                chrBytes = nes2Size(unsigned(payload[5]), byte9 >>> 4, 8_192L);
            } else {
                prgBytes = Math.multiplyExact((long) unsigned(payload[4]), 16_384L);
                chrBytes = Math.multiplyExact((long) unsigned(payload[5]), 8_192L);
            }
        } catch (ArithmeticException overflow) {
            return invalid(
                    format,
                    CompatibilityReason.NES_SIZE_OVERFLOW,
                    new RomAnalysis(
                            0, payload.length, 0, 0, mapper, submapper,
                            trainer, battery, 0, Collections.<RomAnalysis.Warning>emptyList()));
        }

        ArrayList<RomAnalysis.Warning> warnings = new ArrayList<>();
        if (!nes2 && hasDirtyLegacyHeader(payload)) {
            warnings.add(RomAnalysis.Warning.DIRTY_HEADER);
        }
        long expected;
        try {
            expected = Math.addExact(
                    INES_HEADER_BYTES + (trainer ? TRAINER_BYTES : 0L),
                    Math.addExact(prgBytes, chrBytes));
        } catch (ArithmeticException overflow) {
            return invalid(
                    format,
                    CompatibilityReason.NES_SIZE_OVERFLOW,
                    new RomAnalysis(
                            0, payload.length, prgBytes, chrBytes, mapper, submapper,
                            trainer, battery, 0, warnings));
        }
        if (prgBytes == 0) {
            return invalid(
                    format,
                    CompatibilityReason.NES_ZERO_PRG,
                    new RomAnalysis(
                            expected, payload.length, prgBytes, chrBytes, mapper, submapper,
                            trainer, battery, 0, warnings));
        }
        if (payload.length < expected) {
            return invalid(
                    format,
                    CompatibilityReason.NES_TRUNCATED,
                    new RomAnalysis(
                            expected, payload.length, prgBytes, chrBytes, mapper, submapper,
                            trainer, battery, 0, warnings));
        }
        if (payload.length > expected) {
            warnings.add(RomAnalysis.Warning.TRAILING_DATA);
        }
        return new Parsed(
                format,
                CompatibilityDecision.playableNes(),
                new RomAnalysis(
                        expected, payload.length, prgBytes, chrBytes, mapper, submapper,
                        trainer, battery, 0, warnings));
    }

    private static long nes2Size(int lsb, int msbNibble, long unit) {
        if (msbNibble != 0x0F) {
            return Math.multiplyExact(((long) msbNibble << 8) | lsb, unit);
        }
        int exponent = lsb >>> 2;
        int multiplier = ((lsb & 0x03) * 2) + 1;
        if (exponent >= 63) {
            throw new ArithmeticException("NES2 exponential size overflows signed long");
        }
        long base = 1L << exponent;
        return Math.multiplyExact(base, (long) multiplier);
    }

    private static Parsed parseHeaderedFds(byte[] payload) {
        if (payload.length < FDS_HEADER_BYTES) {
            return invalid(
                    RomFormat.FDS,
                    CompatibilityReason.FDS_TRUNCATED,
                    RomAnalysis.basic(payload.length));
        }
        int sides = unsigned(payload[4]);
        if (sides == 0) {
            return invalid(
                    RomFormat.FDS,
                    CompatibilityReason.FDS_INVALID_SIDE_COUNT,
                    RomAnalysis.basic(payload.length));
        }
        long expected = FDS_HEADER_BYTES + sides * (long) FDS_SIDE_BYTES;
        if (payload.length < expected) {
            return invalid(
                    RomFormat.FDS,
                    CompatibilityReason.FDS_TRUNCATED,
                    fdsAnalysis(expected, payload.length, sides, Collections.<RomAnalysis.Warning>emptyList()));
        }
        if (!validFdsSignatures(payload, FDS_HEADER_BYTES, sides)) {
            return invalid(
                    RomFormat.FDS,
                    CompatibilityReason.FDS_INVALID_HEADER,
                    fdsAnalysis(expected, payload.length, sides, Collections.<RomAnalysis.Warning>emptyList()));
        }
        ArrayList<RomAnalysis.Warning> warnings = new ArrayList<>();
        if (payload.length > expected) {
            warnings.add(RomAnalysis.Warning.TRAILING_DATA);
        }
        return unsupportedFds(fdsAnalysis(expected, payload.length, sides, warnings));
    }

    private static Parsed parseHeaderlessFds(byte[] payload) {
        if (payload.length < FDS_SIDE_BYTES || payload.length % FDS_SIDE_BYTES != 0) {
            int expectedSides = (int) Math.max(
                    1L, (payload.length + (long) FDS_SIDE_BYTES - 1L) / FDS_SIDE_BYTES);
            long expected = expectedSides * (long) FDS_SIDE_BYTES;
            return invalid(
                    RomFormat.FDS,
                    CompatibilityReason.FDS_TRUNCATED,
                    fdsAnalysis(expected, payload.length, expectedSides,
                            Collections.<RomAnalysis.Warning>emptyList()));
        }
        int sides = payload.length / FDS_SIDE_BYTES;
        if (!validFdsSignatures(payload, 0, sides)) {
            return invalid(
                    RomFormat.FDS,
                    CompatibilityReason.FDS_INVALID_HEADER,
                    fdsAnalysis(payload.length, payload.length, sides,
                            Collections.<RomAnalysis.Warning>emptyList()));
        }
        return unsupportedFds(fdsAnalysis(
                payload.length, payload.length, sides,
                Collections.<RomAnalysis.Warning>emptyList()));
    }

    private static Parsed unsupportedFds(RomAnalysis analysis) {
        return new Parsed(
                RomFormat.FDS,
                new CompatibilityDecision(
                        CompatibilityState.UNSUPPORTED,
                        CompatibilityReason.FDS_BIOS_API_NOT_IMPLEMENTED),
                analysis);
    }

    private static RomAnalysis fdsAnalysis(
            long expected, long actual, int sides, List<RomAnalysis.Warning> warnings) {
        return new RomAnalysis(
                expected, actual, 0, 0, -1, -1,
                false, false, sides, warnings);
    }

    private static boolean looksLikeHeaderlessFds(byte[] payload) {
        return startsWithAt(payload, 0, FDS_SIDE_SIGNATURE);
    }

    private static boolean validFdsSignatures(byte[] payload, int offset, int sides) {
        for (int side = 0; side < sides; side++) {
            if (!startsWithAt(payload, offset + side * FDS_SIDE_BYTES, FDS_SIDE_SIGNATURE)) {
                return false;
            }
        }
        return true;
    }

    private static Parsed parseUnif(byte[] payload) {
        if (payload.length < 32) {
            return invalid(
                    RomFormat.UNIF,
                    CompatibilityReason.UNIF_INVALID_CHUNK,
                    RomAnalysis.basic(payload.length));
        }
        int cursor = 32;
        boolean hasPrg = false;
        while (cursor < payload.length) {
            if (payload.length - cursor < 8) {
                return invalid(
                        RomFormat.UNIF,
                        CompatibilityReason.UNIF_INVALID_CHUNK,
                        RomAnalysis.basic(payload.length));
            }
            boolean prgChunk = payload[cursor] == 'P'
                    && payload[cursor + 1] == 'R'
                    && payload[cursor + 2] == 'G';
            long length = unsignedInt(payload, cursor + 4);
            cursor += 8;
            if (length > payload.length - cursor) {
                return invalid(
                        RomFormat.UNIF,
                        CompatibilityReason.UNIF_INVALID_CHUNK,
                        RomAnalysis.basic(payload.length));
            }
            if (prgChunk && length > 0) {
                hasPrg = true;
            }
            cursor += (int) length;
        }
        if (!hasPrg) {
            return invalid(
                    RomFormat.UNIF,
                    CompatibilityReason.UNIF_MISSING_PRG,
                    RomAnalysis.basic(payload.length));
        }
        return new Parsed(
                RomFormat.UNIF,
                new CompatibilityDecision(
                        CompatibilityState.UNSUPPORTED,
                        CompatibilityReason.UNIF_PRODUCT_DISABLED),
                RomAnalysis.basic(payload.length));
    }

    private static Parsed invalid(
            RomFormat format, CompatibilityReason reason, RomAnalysis analysis) {
        return new Parsed(
                format,
                new CompatibilityDecision(CompatibilityState.INVALID, reason),
                analysis);
    }

    private static boolean hasDirtyLegacyHeader(byte[] payload) {
        for (int index = 12; index < 16; index++) {
            if (payload[index] != 0) {
                return true;
            }
        }
        return false;
    }

    private static boolean startsWith(byte[] value, byte[] prefix) {
        return startsWithAt(value, 0, prefix);
    }

    private static boolean startsWithAt(byte[] value, int offset, byte[] prefix) {
        if (offset < 0 || value.length - offset < prefix.length) {
            return false;
        }
        for (int index = 0; index < prefix.length; index++) {
            if (value[offset + index] != prefix[index]) {
                return false;
            }
        }
        return true;
    }

    private static int unsigned(byte value) {
        return value & 0xFF;
    }

    private static long unsignedInt(byte[] value, int offset) {
        return (value[offset] & 0xFFL)
                | ((value[offset + 1] & 0xFFL) << 8)
                | ((value[offset + 2] & 0xFFL) << 16)
                | ((value[offset + 3] & 0xFFL) << 24);
    }

    record Parsed(
            RomFormat format,
            CompatibilityDecision compatibility,
            RomAnalysis analysis) {
    }
}
