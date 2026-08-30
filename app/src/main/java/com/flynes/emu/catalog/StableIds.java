package com.flynes.emu.catalog;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;

/** Privacy-safe persistent IDs. Display names and raw provider identifiers never appear in IDs. */
public final class StableIds {
    public static final String RAW_LOCATOR = "RAW";

    private StableIds() {
    }

    public static String packageId(String sourceId, String stableDocumentKey) {
        return "pkg:" + digest(sourceId, stableDocumentKey);
    }

    public static String safSourceId(String treeLocator) {
        return "source:" + digest("SAF_TREE", treeLocator);
    }

    public static String variantId(
            String packageId, String exactRawLocator, String payloadSha256) {
        return "variant:" + digest(
                packageId,
                exactRawLocator,
                RomHashes.normalizedSha256(payloadSha256, "payload SHA-256"));
    }

    public static String provisionalGameId(String payloadSha256) {
        return "game:" + RomHashes.normalizedSha256(payloadSha256, "payload SHA-256");
    }

    public static String entryOutcomeId(String packageId, String exactRawLocator) {
        return "entry:" + digest(packageId, exactRawLocator);
    }

    private static String digest(String... parts) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            for (String part : parts) {
                byte[] encoded = DomainValidation.requireNonBlank(part, "ID input")
                        .getBytes(StandardCharsets.UTF_8);
                digest.update(ByteBuffer.allocate(4).putInt(encoded.length).array());
                digest.update(encoded);
            }
            StringBuilder hex = new StringBuilder(64);
            for (byte value : digest.digest()) {
                hex.append(String.format(Locale.ROOT, "%02X", value & 0xFF));
            }
            return hex.toString();
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException("SHA-256 is required", impossible);
        }
    }
}
