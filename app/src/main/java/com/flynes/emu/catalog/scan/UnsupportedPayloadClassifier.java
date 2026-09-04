package com.flynes.emu.catalog.scan;

import com.flynes.emu.catalog.EntryOutcome;

/** Classifies payloads only after the ROM parser has rejected them. */
final class UnsupportedPayloadClassifier {
    private static final byte[] GAME_BOY_LOGO = new byte[]{
            (byte) 0xCE, (byte) 0xED, 0x66, 0x66, (byte) 0xCC, 0x0D, 0x00, 0x0B,
            0x03, 0x73, 0x00, (byte) 0x83, 0x00, 0x0C, 0x00, 0x0D,
            0x00, 0x08, 0x11, 0x1F, (byte) 0x88, (byte) 0x89, 0x00, 0x0E,
            (byte) 0xDC, (byte) 0xCC, 0x6E, (byte) 0xE6, (byte) 0xDD, (byte) 0xDD,
            (byte) 0xD9, (byte) 0x99, (byte) 0xBB, (byte) 0xBB, 0x67, 0x63,
            0x6E, 0x0E, (byte) 0xEC, (byte) 0xCC, (byte) 0xDD, (byte) 0xDC,
            (byte) 0x99, (byte) 0x9F, (byte) 0xBB, (byte) 0xB9, 0x33, 0x3E};
    private static final int GAME_BOY_LOGO_OFFSET = 0x104;
    private static final int TEXT_INSPECTION_LIMIT = 4096;

    private UnsupportedPayloadClassifier() {
    }

    static EntryOutcome.Reason classify(byte[] payload) {
        if (hasZipSignature(payload)) {
            return EntryOutcome.Reason.NESTED_ARCHIVE;
        }
        if (payload.length >= 2 && payload[0] == 'M' && payload[1] == 'Z') {
            return EntryOutcome.Reason.EXECUTABLE;
        }
        if (hasGameBoyLogo(payload)) {
            return EntryOutcome.Reason.GAME_BOY;
        }
        return isSidecarText(payload)
                ? EntryOutcome.Reason.SIDECAR : EntryOutcome.Reason.UNKNOWN_FORMAT;
    }

    static boolean hasZipSignature(byte[] payload) {
        return payload.length >= 4
                && payload[0] == 'P'
                && payload[1] == 'K'
                && ((payload[2] == 3 && payload[3] == 4)
                || (payload[2] == 5 && payload[3] == 6)
                || (payload[2] == 7 && payload[3] == 8));
    }

    private static boolean hasGameBoyLogo(byte[] payload) {
        if (payload.length < GAME_BOY_LOGO_OFFSET + GAME_BOY_LOGO.length) {
            return false;
        }
        for (int index = 0; index < GAME_BOY_LOGO.length; index++) {
            if (payload[GAME_BOY_LOGO_OFFSET + index] != GAME_BOY_LOGO[index]) {
                return false;
            }
        }
        return true;
    }

    private static boolean isSidecarText(byte[] payload) {
        int inspected = Math.min(payload.length, TEXT_INSPECTION_LIMIT);
        for (int index = 0; index < inspected; index++) {
            int value = payload[index] & 0xFF;
            if (value == 0
                    || (value < 0x20 && value != '\t' && value != '\n' && value != '\r')) {
                return false;
            }
        }
        return true;
    }
}
